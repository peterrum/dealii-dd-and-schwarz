#pragma once

#include "dof_tools.h"
#include "grid_generator.h"
#include "grid_tools.h"

enum class WeightingType
{
  none,
  left,
  right,
  symm
};

template <typename PreconditionerType,
          typename SparseMatrixType,
          typename SparsityPattern>
class DomainPreconditioner
{
public:
  DomainPreconditioner(const WeightingType weighting_type = WeightingType::none)
    : weighting_type(weighting_type)
  {}

  template <typename GlobalSparseMatrixType, typename GlobalSparsityPattern>
  void
  initialize(const GlobalSparseMatrixType &global_sparse_matrix,
             const GlobalSparsityPattern & global_sparsity_pattern,
             const IndexSet &              local_index_set,
             const IndexSet &              active_index_set = {})
  {
    SparseMatrixTools::restrict_to_serial_sparse_matrix(global_sparse_matrix,
                                                        global_sparsity_pattern,
                                                        local_index_set,
                                                        active_index_set,
                                                        sparse_matrix,
                                                        sparsity_pattern);

    preconditioner.initialize(sparse_matrix);

    IndexSet union_index_set = local_index_set;
    union_index_set.add_indices(active_index_set);

    local_src.reinit(union_index_set.n_elements());
    local_dst.reinit(union_index_set.n_elements());

    const auto comm = global_sparse_matrix.get_mpi_communicator();

    if (active_index_set.size() == 0)
      this->partitioner =
        std::make_shared<Utilities::MPI::Partitioner>(local_index_set, comm);
    else
      this->partitioner =
        std::make_shared<Utilities::MPI::Partitioner>(local_index_set,
                                                      active_index_set,
                                                      comm);
  }

  template <typename VectorType>
  void
  vmult(VectorType &dst, const VectorType &src) const
  {
    VectorType dst_, src_, multiplicity;
    dst_.reinit(partitioner);
    src_.reinit(partitioner);

    if (weighting_type != WeightingType::none)
      {
        multiplicity.reinit(partitioner);
        for (unsigned int i = 0; i < local_src.size(); ++i)
          multiplicity.local_element(i) = 1.0;
        multiplicity.compress(VectorOperation::add);

        for (auto &i : multiplicity)
          i = (weighting_type == WeightingType::symm) ? std::sqrt(1.0 / i) :
                                                        (1.0 / i);

        multiplicity.update_ghost_values();
      }

    src_.copy_locally_owned_data_from(src); // TODO: inplace
    src_.update_ghost_values();

    if (weighting_type == WeightingType::symm ||
        weighting_type == WeightingType::right)
      for (unsigned int i = 0; i < local_src.size(); ++i)
        src_.local_element(i) *= multiplicity.local_element(i);

    for (unsigned int i = 0; i < local_src.size(); ++i)
      local_src[i] = src_.local_element(i);

    preconditioner.vmult(local_dst, local_src);

    for (unsigned int i = 0; i < local_dst.size(); ++i)
      dst_.local_element(i) = local_dst[i];

    src_.zero_out_ghost_values();

    if (weighting_type == WeightingType::symm ||
        weighting_type == WeightingType::left)
      for (unsigned int i = 0; i < local_dst.size(); ++i)
        dst_.local_element(i) *= multiplicity.local_element(i);

    dst_.compress(VectorOperation::add);
    dst.copy_locally_owned_data_from(dst_); // TODO: inplace
  }

private:
  SparsityPattern    sparsity_pattern;
  SparseMatrixType   sparse_matrix;
  PreconditionerType preconditioner;

  std::shared_ptr<Utilities::MPI::Partitioner> partitioner;

  mutable Vector<typename SparseMatrixType::value_type> local_src, local_dst;

  const WeightingType weighting_type;
};

template <typename Number, int dim, int spacedim = dim>
class InverseCellBlockPreconditioner
{
public:
  InverseCellBlockPreconditioner(
    const DoFHandler<dim, spacedim> &dof_handler,
    const WeightingType              weighting_type = WeightingType::none)
    : dof_handler(dof_handler)
    , weighting_type(weighting_type)
  {}

  template <typename GlobalSparseMatrixType, typename GlobalSparsityPattern>
  void
  initialize(const GlobalSparseMatrixType &global_sparse_matrix,
             const GlobalSparsityPattern & global_sparsity_pattern)
  {
    SparseMatrixTools::restrict_to_cells(global_sparse_matrix,
                                         global_sparsity_pattern,
                                         dof_handler,
                                         blocks);

    for (auto &block : blocks)
      if (block.m() > 0 && block.n() > 0)
        block.gauss_jordan();
  }

  template <typename VectorType>
  void
  vmult(VectorType &dst, const VectorType &src) const
  {
    dst = 0.0;
    src.update_ghost_values();

    Vector<double> vector_src, vector_dst, vector_weights;

    VectorType weights;

    if (weighting_type != WeightingType::none)
      {
        weights.reinit(src);

        for (const auto &cell : dof_handler.active_cell_iterators())
          {
            if (cell->is_locally_owned() == false)
              continue;

            const unsigned int dofs_per_cell = cell->get_fe().n_dofs_per_cell();
            vector_weights.reinit(dofs_per_cell);

            for (unsigned int i = 0; i < dofs_per_cell; ++i)
              vector_weights[i] = 1.0;

            cell->distribute_local_to_global(vector_weights, weights);
          }

        weights.compress(VectorOperation::add);
        for (auto &i : weights)
          i = (weighting_type == WeightingType::symm) ? std::sqrt(1.0 / i) :
                                                        (1.0 / i);
        weights.update_ghost_values();
      }


    for (const auto &cell : dof_handler.active_cell_iterators())
      {
        if (cell->is_locally_owned() == false)
          continue;

        const unsigned int dofs_per_cell = cell->get_fe().n_dofs_per_cell();

        vector_src.reinit(dofs_per_cell);
        vector_dst.reinit(dofs_per_cell);
        if (weighting_type != WeightingType::none)
          vector_weights.reinit(dofs_per_cell);

        cell->get_dof_values(src, vector_src);
        if (weighting_type != WeightingType::none)
          cell->get_dof_values(weights, vector_weights);

        if (weighting_type == WeightingType::symm ||
            weighting_type == WeightingType::right)
          for (unsigned int i = 0; i < dofs_per_cell; ++i)
            vector_src[i] *= vector_weights[i];

        blocks[cell->active_cell_index()].vmult(vector_dst, vector_src);

        if (weighting_type == WeightingType::symm ||
            weighting_type == WeightingType::left)
          for (unsigned int i = 0; i < dofs_per_cell; ++i)
            vector_dst[i] *= vector_weights[i];

        cell->distribute_local_to_global(vector_dst, dst);
      }

    src.zero_out_ghost_values();
    dst.compress(VectorOperation::add);
  }

private:
  const DoFHandler<dim, spacedim> &dof_handler;
  std::vector<FullMatrix<Number>>  blocks;

  const WeightingType weighting_type;
};


template <typename VectorType>
class PreconditionerBase
{
public:
  using vector_type = VectorType;

  virtual void
  vmult(VectorType &dst, const VectorType &src) const = 0;
};



template <typename VectorType, typename RestrictorType>
class RestrictedPreconditionerBase : public PreconditionerBase<VectorType>
{
public:
  using Number = typename VectorType::value_type;

  RestrictedPreconditionerBase() = default;

  /**
   * Perform matrix-vector product by looping over all blocks,
   * restricting the source vector, apply the inverted
   * block matrix, and distributing the result back into the
   * global vector.
   */
  void
  vmult(VectorType &dst, const VectorType &src) const override
  {
    dealii::Vector<Number> src_, dst_;

    dst = 0.0;
    src.update_ghost_values();

    for (unsigned int c = 0; c < blocks.size(); ++c)
      {
        const auto block = blocks[c];

        if (block.m() == 0 || block.n() == 0)
          continue;

        AssertDimension(block.m(), block.n());

        src_.reinit(block.m());
        dst_.reinit(block.m());

        restrictor->read_dof_values(c, src, src_);

        block.vmult(dst_, src_);

        restrictor->distribute_dof_values(c, dst_, dst);
      }

    dst.compress(dealii::VectorOperation::add);
    src.zero_out_ghost_values();
  }

protected:
  /**
   * Initialize class with a sparse matrix and a restrictor.
   */
  void
  initialize_internal(const std::shared_ptr<const RestrictorType> &restrictor)
  {
    this->restrictor = restrictor;

    for (auto &block : blocks)
      if (block.m() > 0 && block.n() > 0)
        block.gauss_jordan();
  }

  std::shared_ptr<const RestrictorType> restrictor;
  std::vector<FullMatrix<Number>>       blocks;
};



/**
 * An additive Schwarz preconditioner. It is fully defined by a
 * sparse system matrix and a restrictor. The restrictor is used
 * to extract (overlapping) blocks from the matrix and potentially
 * to weights the contributations.
 */
template <typename VectorType, typename RestrictorType>
class AdditiveSchwarzPreconditioner
  : public RestrictedPreconditionerBase<VectorType, RestrictorType>
{
public:
  using Number = typename VectorType::value_type;

  AdditiveSchwarzPreconditioner() = default;

  template <typename GlobalSparseMatrixType, typename GlobalSparsityPattern>
  AdditiveSchwarzPreconditioner(
    const std::shared_ptr<const RestrictorType> &restrictor,
    const GlobalSparseMatrixType &               global_sparse_matrix,
    const GlobalSparsityPattern &                global_sparsity_pattern)
  {
    this->initialize(restrictor, global_sparse_matrix, global_sparsity_pattern);
  }

  /**
   * Initialize class with a sparse matrix and a restrictor.
   */
  template <typename GlobalSparseMatrixType, typename GlobalSparsityPattern>
  void
  initialize(const std::shared_ptr<const RestrictorType> &restrictor,
             const GlobalSparseMatrixType &               global_sparse_matrix,
             const GlobalSparsityPattern &global_sparsity_pattern)
  {
    dealii::SparseMatrixTools::restrict_to_full_matrices(
      global_sparse_matrix,
      global_sparsity_pattern,
      restrictor->get_indices(),
      this->blocks);

    this->initialize_internal(restrictor);
  }
};



/**
 * TODO.
 */
template <typename VectorType, typename RestrictorType>
class SubMeshPreconditioner
  : public RestrictedPreconditionerBase<VectorType, RestrictorType>
{
public:
  using Number = typename VectorType::value_type;

  struct AdditionalData
  {
    AdditionalData(const unsigned int sub_mesh_approximation = 1)
      : sub_mesh_approximation(sub_mesh_approximation)
    {}

    unsigned int sub_mesh_approximation;
  };

  SubMeshPreconditioner() = default;

  template <typename OperatorType>
  SubMeshPreconditioner(
    const std::shared_ptr<const OperatorType> &  op,
    const std::shared_ptr<const RestrictorType> &restrictor,
    const AdditionalData &additional_data = AdditionalData())
  {
    this->initialize(op, restrictor, additional_data);
  }

  template <typename OperatorType>
  void
  initialize(const std::shared_ptr<const OperatorType> &  op,
             const std::shared_ptr<const RestrictorType> &restrictor,
             const AdditionalData &additional_data = AdditionalData())
  {
    for (const auto &cell : op->get_dof_handler().active_cell_iterators())
      if (cell->is_locally_owned())
        {
          auto sub_cells =
            dealii::GridTools::extract_all_surrounding_cells_cartesian<
              OperatorType::dimension>(cell,
                                       additional_data.sub_mesh_approximation);

          Triangulation<OperatorType::dimension> sub_tria;
          dealii::GridGenerator::create_mesh_from_cells(sub_cells, sub_tria);

          OperatorType sub_op(op->get_mapping(),
                              sub_tria,
                              op->get_fe(),
                              op->get_quadrature());

          // TODO: make sub_cells local

          const auto indices =
            dealii::DoFTools::get_dof_indices_cell_with_overlap(
              sub_op.get_dof_handler(),
              sub_cells,
              this->restrictor->get_n_overlap());

          // TODO: extract submatrix
        }

    this->initialize_internal(restrictor);
  }
};
