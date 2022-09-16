#include <deal.II/base/exceptions.h>

#include <array>
#include <iostream>
#include <vector>

using namespace dealii;

template <typename Number>
void
gather(const std::vector<Number> &      global_vector,
       const unsigned int               degree,
       const std::vector<unsigned int> &dofs_of_cell,
       const std::vector<unsigned int> &orientations, // TODO: compress
       std::vector<Number> &            local_vector)
{
  // helper function to reorientate indices on line
  const auto reorientate_line = [degree](const unsigned int i,
                                         const bool         flag) {
    if (flag)
      return degree - i - 2;
    else
      return i;
  };

  unsigned int counter    = 0;
  unsigned int offset     = 0;
  unsigned int compressed = 0;

  for (unsigned int j = 0; j <= degree; ++j)
    {
      const auto indices = dofs_of_cell.begin() + compressed * 3;

      // bottom layer (j=0; vertex-line-vertex)
      if ((orientations[2] == 1) && (j == 0))
        {
          const bool flag = orientations[2] == 1;

          // vertex 0
          local_vector[counter++] = global_vector[indices[0]];

          // line 2
          for (unsigned int i = 0; i < degree - 1; ++i)
            local_vector[counter++] =
              global_vector[indices[1] + reorientate_line(i, flag)];

          // vertex 1
          local_vector[counter++] = global_vector[indices[2]];
        }
      else if ((orientations[0] == 1 || orientations[1] == 1) &&
               ((0 < j) &&
                (j < degree))) // middle layers (0<j<p; line-quad-line)
        {
          const bool flag0 = orientations[0] == 1;
          const bool flag1 = orientations[1] == 1;

          // line 0
          local_vector[counter++] =
            global_vector[indices[0] + reorientate_line(offset, flag0)];

          // quad 0
          for (unsigned int i = 0; i < degree - 1; ++i)
            local_vector[counter++] =
              global_vector[indices[1] + offset * (degree - 1) + i];

          // line 1
          local_vector[counter++] =
            global_vector[indices[2] + reorientate_line(offset, flag1)];
        }
      else if ((orientations[3] == 1) &&
               (j == degree)) // top layer (j=p; vertex-line-vertex)
        {
          const bool flag = orientations[3] == 1;

          // vertex 2
          local_vector[counter++] = global_vector[indices[0]];

          // line 3
          for (unsigned int i = 0; i < degree - 1; ++i)
            local_vector[counter++] =
              global_vector[indices[1] + reorientate_line(i, flag)];

          // vertex 3
          local_vector[counter++] = global_vector[indices[2]];
        }
      else
        {
          local_vector[counter++] = global_vector[indices[0] + offset];

          for (unsigned int i = 0; i < degree - 1; ++i)
            local_vector[counter++] =
              global_vector[indices[1] + offset * (degree - 1) + i];

          local_vector[counter++] = global_vector[indices[2] + offset];
        }

      if ((j == 0) || (j == (degree - 1)))
        {
          compressed++;
          offset = 0;
        }
      else
        offset++;
    }
}

int
main(int argc, char *argv[])
{
  AssertThrow(argc == 6, ExcNotImplemented());

  const unsigned int degree = atoi(argv[1]);

  std::vector<unsigned int> orientations(4);

  for (unsigned int i = 0; i < 4; ++i)
    orientations[i] = atoi(argv[2 + i]);

  // setup dpo object
  std::vector<std::pair<unsigned int, unsigned int>> dpo;
  dpo.emplace_back(4, 1);
  dpo.emplace_back(4, degree - 1);
  dpo.emplace_back(1, (degree - 1) * (degree - 1));

  // determine staring dof each entity
  std::vector<unsigned int> dofs;

  unsigned int dof_counter = 0;
  for (const auto &entry : dpo)
    for (unsigned int i = 0; i < entry.first; ++i)
      {
        dofs.emplace_back(dof_counter);
        dof_counter += entry.second;
      }

  // determine starting dof of each entity of cell
  std::vector<unsigned int> entities_of_cell = {0, 6, 1, 4, 8, 5, 2, 7, 3};

  std::vector<unsigned int> dofs_of_cell;

  for (const auto i : entities_of_cell)
    dofs_of_cell.emplace_back(dofs[i]);

  // create dummy global vector
  std::vector<double> global_vector;
  for (unsigned int i = 0; i < dof_counter; ++i)
    global_vector.emplace_back(i);

  // gather values and print to terminal
  std::vector<double> local_vector(dof_counter);

  gather(global_vector, degree, dofs_of_cell, orientations, local_vector);

  for (unsigned int j = 0, c = 0; j <= degree; ++j)
    {
      for (unsigned int j = 0; j <= degree; ++j, ++c)
        printf("%4.0f", local_vector[c]);

      printf("\n");
    }
  printf("\n");
}