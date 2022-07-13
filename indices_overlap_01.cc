#include <deal.II/base/quadrature_lib.h>

#include <deal.II/fe/fe_nothing.h>
#include <deal.II/fe/fe_q.h>
#include <deal.II/fe/fe_tools.h>
#include <deal.II/fe/fe_values.h>
#include <deal.II/fe/mapping_q.h>
#include <deal.II/fe/mapping_q_cache.h>

#include <deal.II/grid/grid_generator.h>

#include <deal.II/numerics/data_out.h>

#include "include/grid_generator.h"
#include "include/grid_tools.h"

namespace dealii
{
  namespace DoFTools
  {
    template <int dim>
    std::vector<types::global_dof_index>
    get_dof_indices_cell_with_overlap(
      const DoFHandler<dim> &                   dof_handler,
      const std::array<typename Triangulation<dim>::cell_iterator,
                       Utilities::pow(3, dim)> &cells,
      unsigned int                              n_overlap = 0)
    {
      AssertDimension(dof_handler.get_fe_collection().size(), 1);

      const auto &fe = dof_handler.get_fe();

      AssertDimension(fe.n_components(), 1);

      const unsigned int fe_degree = fe.tensor_degree();

      const auto lexicographic_to_hierarchic_numbering =
        Utilities::invert_permutation(
          FETools::hierarchic_to_lexicographic_numbering<dim>(fe_degree));

      const auto get_lexicographic_dof_indices = [&](const auto &cell) {
        const auto n_dofs_per_cell = fe.n_dofs_per_cell();

        std::vector<types::global_dof_index> indices_hierarchic(
          n_dofs_per_cell);
        std::vector<types::global_dof_index> indices_lexicographic(
          n_dofs_per_cell);

        (typename DoFHandler<dim>::cell_iterator(&cell->get_triangulation(),
                                                 cell->level(),
                                                 cell->index(),
                                                 &dof_handler))
          ->get_dof_indices(indices_hierarchic);

        for (unsigned int i = 0; i < n_dofs_per_cell; ++i)
          indices_lexicographic[i] =
            indices_hierarchic[lexicographic_to_hierarchic_numbering[i]];

        return indices_lexicographic;
      };

      if (n_overlap <= 1)
        {
          const auto dof_indices =
            get_lexicographic_dof_indices(cells[cells.size() / 2]);

          if (n_overlap == 1)
            return dof_indices;

          Assert(false, ExcNotImplemented());
        }
      else
        {
          Assert(false, ExcNotImplemented());
        }
    }
  } // namespace DoFTools
} // namespace dealii

using namespace dealii;

template <int dim>
void
test(unsigned int fe_degree, unsigned int n_global_refinements)
{
  Triangulation<dim> tria;
  GridGenerator::hyper_cube(tria);
  tria.refine_global(n_global_refinements);

  FE_Q<dim>       fe(fe_degree);
  DoFHandler<dim> dof_handler(tria);
  dof_handler.distribute_dofs(fe);

  for (const auto &cell : tria.active_cell_iterators())
    if (cell->is_locally_owned())
      {
        const auto cells =
          GridTools::extract_all_surrounding_cells_cartesian<dim>(cell);

        const auto dof_indices =
          DoFTools::get_dof_indices_cell_with_overlap(dof_handler, cells, 1);

        for (const auto index : dof_indices)
          std::cout << index << " ";
        std::cout << std::endl;
      }
}

int
main(int argc, char *argv[])
{
  Utilities::MPI::MPI_InitFinalize mpi_initialization(argc, argv, 1);

  const unsigned int dim       = (argc >= 2) ? std::atoi(argv[1]) : 2;
  const unsigned int fe_degree = (argc >= 3) ? std::atoi(argv[2]) : 2;
  const unsigned int n_global_refinements =
    (argc >= 4) ? std::atoi(argv[3]) : 3;

  if (dim == 2)
    test<2>(fe_degree, n_global_refinements);
  else
    AssertThrow(false, ExcNotImplemented());
}
