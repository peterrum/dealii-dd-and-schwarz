#pragma once

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
        multiplicity = 1.0;
        multiplicity.compress(VectorOperation::add);
        multiplicity.update_ghost_values();

        for (auto &i : multiplicity)
          i =
            (weighting_type == WeightingType::symm) ? std::sqrt(1 / i) : 1 / i;
      }

    src_.copy_locally_owned_data_from(src); // TODO: inplace
    src_.update_ghost_values();

    if (weighting_type == WeightingType::symm ||
        weighting_type == WeightingType::right)
      src_.scale(multiplicity);

    for (unsigned int i = 0; i < local_src.size(); ++i)
      local_src[i] = src_.local_element(i);

    preconditioner.vmult(local_dst, local_src);

    for (unsigned int i = 0; i < local_dst.size(); ++i)
      dst_.local_element(i) = local_dst[i];

    src_.zero_out_ghost_values();

    if (weighting_type == WeightingType::symm ||
        weighting_type == WeightingType::left)
      dst_.scale(multiplicity);

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
  InverseCellBlockPreconditioner(const DoFHandler<dim, spacedim> &dof_handler)
    : dof_handler(dof_handler)
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

    Vector<double> vector_src;
    Vector<double> vector_dst;

    for (const auto &cell : dof_handler.active_cell_iterators())
      {
        if (cell->is_locally_owned() == false)
          continue;

        const unsigned int dofs_per_cell = cell->get_fe().n_dofs_per_cell();

        vector_src.reinit(dofs_per_cell);
        vector_dst.reinit(dofs_per_cell);

        cell->get_dof_values(src, vector_src);

        blocks[cell->active_cell_index()].vmult(vector_dst, vector_src);

        cell->distribute_local_to_global(vector_dst, dst);
      }

    src.zero_out_ghost_values();
    dst.compress(VectorOperation::add);
  }

private:
  const DoFHandler<dim, spacedim> &dof_handler;
  std::vector<FullMatrix<Number>>  blocks;
};