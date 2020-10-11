#include <vector>
#include <iostream>
#include <fstream>
#include <random>
#include <chrono>
#include <map>
#include <set>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include "../libs/voro++-0.4.6/src/voro++.hh"
#include "../lib/lib_vec.hpp"

class FilamentNetworkProblem
{
public:
  std::vector<double> boxZeroPoint_;
  std::vector<double> boxsize_;
  uint numfil_;
};

class Voronoi
{
  std::string outputFolderPath_;
  std::string fileName_;
  std::vector<double> boxsize_;
  double seed_;
  uint voronoiParticleCount_;
  uint currnumfils_;
  // position of center of particles
  std::vector<std::vector<double>> particlePositions_ = std::vector<std::vector<double>>();
  // contains vertex id with its position
  std::vector<std::vector<double>> uniqueVertices_ = std::vector<std::vector<double>>();
  // contains edges and the respective nodes it is attached to
  std::vector<std::vector<unsigned int>> uniqueVertexEdgePartners_ = std::vector<std::vector<unsigned int>>();
  // contains order of a vertices
  std::vector<unsigned int> uniqueVertexOrders_ = std::vector<unsigned int>();
  // nodes to edges (contains dead nodes)
  std::vector<std::vector<unsigned int>> node_to_edges_;
  // the following map does not contain dead nodes, i.e. nodes with z == 0
  // so in here only really existing nodes
  std::map<unsigned int, std::vector<double>> uniqueVertices_map_;
  // all really existing nodes with their respective edges
  std::map<unsigned int, std::vector<unsigned int>> uniqueVertices_to_Edge_map_;
  // a vector containing all vertex ids of really existing verteces. Note
  // that index of vector is not equal to to node id
  std::vector<unsigned int> uniqueVertices_for_random_draw_;
  // shifted vertex positions
  std::vector<std::vector<double>> vtxs_shifted_;
  // contains all finite element node ids that belong to vertex
  std::vector<std::vector<unsigned int>> vertexNodeIds_ = std::vector<std::vector<unsigned int>>();

  public: void ComputeVoronoi(FilamentNetworkProblem *filanetprob)
  {
    double one_third = 1.0 / 3.0;

    boxsize_ = filanetprob->boxsize_;

    std::cout << "\n\nNetwork Generation started." << std::endl;
    std::cout << "------------------------------------------------------\n"
              << std::flush;
    std::cout << "1) Computing Voronoi.\n"
              << std::flush;
    auto start_voro = std::chrono::high_resolution_clock::now();

    std::random_device rd;
    std::mt19937 gen(rd());
    gen.seed(this->seed_);
    // random number between 0 and 1
    std::uniform_real_distribution<> dis_uni(0, 1);

    // reset variables in case of unsuccessful computation
    this->particlePositions_.clear();
    this->uniqueVertices_.clear();
    this->uniqueVertexEdgePartners_.clear();
    this->uniqueVertexOrders_.clear();

    double compareTolerance = 1e-13;
    std::string nullString = "";

    double x_min;
    double x_max;
    double y_min;
    double y_max;
    double z_min;
    double z_max;

    x_min = filanetprob->boxZeroPoint_[0] - filanetprob->boxsize_[0] / 2;
    x_max = filanetprob->boxZeroPoint_[0] + filanetprob->boxsize_[0] / 2;
    y_min = filanetprob->boxZeroPoint_[1] - filanetprob->boxsize_[1] / 2;
    y_max = filanetprob->boxZeroPoint_[1] + filanetprob->boxsize_[1] / 2;
    z_min = filanetprob->boxZeroPoint_[2] - filanetprob->boxsize_[2] / 2;
    z_max = filanetprob->boxZeroPoint_[2] + filanetprob->boxsize_[2] / 2;
    int n_x = 1, n_y = 1, n_z = 1;

    unsigned int particleCount = this->voronoiParticleCount_;
    // allocate
    // this->cellVertexOrders_ = std::vector<std::vector<uint32_t>>(particleCount);
    // this->cellRads_ = std::vector<std::vector<double>>(particleCount);
    // this->cellRadAvg_ = std::vector<double>(particleCount);
    // this->cellRadMax_ = std::vector<double>(particleCount);
    // this->cellRadMin_ = std::vector<double>(particleCount);
    // this->cellRad_in_ = std::vector<double>(particleCount);

    double x, y, z;

    // Create a container with the geometry given above. Allocate space for
    // eight particles within each computational block
    voro::container con(x_min, x_max, y_min, y_max, z_min, z_max, n_x, n_y, n_z,
                        true,
                        true,
                        true, 8);

    this->particlePositions_.reserve(particleCount);

    for (unsigned int i = 0; i < particleCount; ++i)
    {
      x = x_min + dis_uni(gen) * (x_max - x_min);
      y = y_min + dis_uni(gen) * (y_max - y_min);
      z = z_min + dis_uni(gen) * (z_max - z_min);
      con.put(i, x, y, z);
      this->particlePositions_.emplace_back(std::vector<double>{x, y, z});
    }

    voro::c_loop_all loop = voro::c_loop_all(con);

    // cell index = particle index
    unsigned int cellIndex = 0;
    unsigned int cellCount = particleCount;
    std::vector<double> vertices = std::vector<double>();

    int debug_lastUnique = -1;
    unsigned int count = 1;
    if (loop.start())
      do
      {
        voro::voronoicell_neighbor cell;
        if (con.compute_cell(cell, loop))
        {
          // unused
          int cellId;
          double x, y, z, radius;
          loop.pos(cellId, x, y, z, radius);

          cell.vertices(x, y, z, vertices);

          std::vector<std::vector<double>> cellVertices = std::vector<std::vector<double>>(vertices.size() / 3);

          for (unsigned int vertexIndex = 0; vertexIndex < vertices.size(); vertexIndex += 3)
          {
            cellVertices[vertexIndex / 3] = std::vector<double>{vertices[vertexIndex],
                                                                vertices[vertexIndex + 1],
                                                                vertices[vertexIndex + 2]};
          }

          unsigned int vertexCount = cellVertices.size();

          // statistical data for network characterization
          std::vector<double> cellRads = std::vector<double>();
          double maxRad = 0;
          double minRad = 0;
          double avgRad = 0;

          std::vector<int> vertexOrders;
          cell.vertex_orders(vertexOrders);

          std::vector<std::vector<int>> vertexPartners =
              std::vector<std::vector<int>>(vertexCount);
          std::vector<std::vector<uint32_t>> cellVertexEdgePartners =
              std::vector<std::vector<uint32_t>>();
          std::vector<std::vector<uint32_t>> uniqueVertexEdgePartners =
              std::vector<std::vector<uint32_t>>();

          unsigned int uniquePartner1PositionIndex;
          unsigned int uniquePartner2PositionIndex;

          for (int vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
          {
            // edges
            vertexPartners[vertexIndex] = std::vector<int>(vertexOrders[vertexIndex]);

            for (int partnerIndex = 0; partnerIndex < vertexOrders[vertexIndex]; ++partnerIndex)
              vertexPartners[vertexIndex][partnerIndex] = cell.ed[vertexIndex][partnerIndex];

            // statistical
            double radius =
                std::sqrt((cellVertices[vertexIndex][0] - this->particlePositions_[cellIndex][0]) *
                              (cellVertices[vertexIndex][0] -
                               this->particlePositions_[cellIndex][0]) +
                          (cellVertices[vertexIndex][1] -
                           this->particlePositions_[cellIndex][1]) *
                              (cellVertices[vertexIndex][1] -
                               this->particlePositions_[cellIndex][1]) +
                          (cellVertices[vertexIndex][2] -
                           this->particlePositions_[cellIndex][2]) *
                              (cellVertices[vertexIndex][2] -
                               this->particlePositions_[cellIndex][2]));
            cellRads.push_back(radius);
            avgRad += radius;
            if (radius > maxRad)
              maxRad = radius;
            if (radius < minRad | minRad == 0)
              minRad = radius;

            // actual network creation from output of voro++
            std::vector<double> vertexPositionCurrent = cellVertices[vertexIndex];
            bool vertexIsUnique = true;

            for (int uniqueVertexIndex = 0; uniqueVertexIndex < this->uniqueVertices_.size(); ++uniqueVertexIndex)
            {
              if (std::abs(this->uniqueVertices_[uniqueVertexIndex][0] - vertexPositionCurrent[0]) < compareTolerance &&
                  std::abs(this->uniqueVertices_[uniqueVertexIndex][1] - vertexPositionCurrent[1]) < compareTolerance &&
                  std::abs(this->uniqueVertices_[uniqueVertexIndex][2] - vertexPositionCurrent[2]) < compareTolerance)
              {
                vertexIsUnique = false;
                uniquePartner1PositionIndex = uniqueVertexIndex;
                // std::cout << "1: " <<
                // std::to_string(uniquePartner1PositionIndex) << "\n";
              }
            }

            if (vertexIsUnique)
            {
              uniquePartner1PositionIndex = this->uniqueVertices_.size();
              this->uniqueVertices_.push_back(vertexPositionCurrent);

              if (debug_lastUnique > 0 && uniquePartner1PositionIndex < debug_lastUnique)
              {
                std::cout << "1U: " << std::to_string(uniquePartner1PositionIndex)
                          << "\n";
                std::cout << "^ ERROR! Unique vertex id is not higher than last! "
                             "Press any key to continue...\n";
                getline(std::cin, nullString);
              }
              debug_lastUnique++;
            }

            std::vector<bool> onPlanesPartner1 = std::vector<bool>{
                this->VertexIsOnHighPlane(filanetprob, uniquePartner1PositionIndex, 0) ||
                    this->VertexIsOnLowPlane(filanetprob, uniquePartner1PositionIndex, 0),
                this->VertexIsOnHighPlane(filanetprob, uniquePartner1PositionIndex, 1) ||
                    this->VertexIsOnLowPlane(filanetprob, uniquePartner1PositionIndex, 1),
                this->VertexIsOnHighPlane(filanetprob, uniquePartner1PositionIndex, 2) ||
                    this->VertexIsOnLowPlane(filanetprob, uniquePartner1PositionIndex, 2)};

            unsigned int edgesCreated = 0;
            for (int partnerIndex = 0; partnerIndex < vertexOrders[vertexIndex]; ++partnerIndex)
            {
              int vertexPartnerIndexCurrent = vertexPartners[vertexIndex][partnerIndex];
              std::vector<double> vertexPartnerPositionCurrent = cellVertices[vertexPartnerIndexCurrent];

              bool vertexPartnerIsUnique = true;
              for (int uniqueVertexIndex = 0; uniqueVertexIndex < this->uniqueVertices_.size(); ++uniqueVertexIndex)
              {
                if (std::abs(this->uniqueVertices_[uniqueVertexIndex][0] - vertexPartnerPositionCurrent[0]) < compareTolerance &&
                    std::abs(this->uniqueVertices_[uniqueVertexIndex][1] - vertexPartnerPositionCurrent[1]) < compareTolerance &&
                    std::abs(this->uniqueVertices_[uniqueVertexIndex][2] - vertexPartnerPositionCurrent[2]) < compareTolerance)
                {
                  vertexPartnerIsUnique = false;
                  uniquePartner2PositionIndex = uniqueVertexIndex;
                  // std::cout << "2: " <<
                  // std::to_string(uniquePartner2PositionIndex) << "\n";
                }
              }

              if (vertexPartnerIsUnique)
              {
                uniquePartner2PositionIndex = this->uniqueVertices_.size();
                this->uniqueVertices_.push_back(vertexPartnerPositionCurrent);

                if (debug_lastUnique > 0 && uniquePartner2PositionIndex < debug_lastUnique)
                {
                  std::cout << "2U: "
                            << std::to_string(uniquePartner2PositionIndex)
                            << "\n";
                  std::cout << "^ ERROR! Unique vertex id is not higher than "
                               "last! Press any key to continue...\n";
                  getline(std::cin, nullString);
                }
                debug_lastUnique++;
              }

              if (uniquePartner1PositionIndex == uniquePartner2PositionIndex)
              {
                std::cout << "Error: VertexUIds equal. This should not happen!\n";
                continue;
              }

              bool edgeIsUnique = true;
              for (int uniqueVertexEdgePartnersIndex = 0; uniqueVertexEdgePartnersIndex < this->uniqueVertexEdgePartners_.size(); ++uniqueVertexEdgePartnersIndex)
                if ((this->uniqueVertexEdgePartners_[uniqueVertexEdgePartnersIndex][0] == uniquePartner1PositionIndex &&
                     this->uniqueVertexEdgePartners_[uniqueVertexEdgePartnersIndex][1] == uniquePartner2PositionIndex) ||
                    (this->uniqueVertexEdgePartners_[uniqueVertexEdgePartnersIndex][0] == uniquePartner2PositionIndex &&
                     this->uniqueVertexEdgePartners_[uniqueVertexEdgePartnersIndex][1] == uniquePartner1PositionIndex))
                {
                  edgeIsUnique = false;
                }

              if (edgeIsUnique)
              {
                //! OLD
                std::vector<bool> onPlanesPartner2 = std::vector<bool>{
                    this->VertexIsOnHighPlane(filanetprob,
                                              uniquePartner2PositionIndex, 0) ||
                        this->VertexIsOnLowPlane(filanetprob,
                                                 uniquePartner2PositionIndex, 0),
                    this->VertexIsOnHighPlane(filanetprob,
                                              uniquePartner2PositionIndex, 1) ||
                        this->VertexIsOnLowPlane(filanetprob,
                                                 uniquePartner2PositionIndex, 1),
                    this->VertexIsOnHighPlane(filanetprob,
                                              uniquePartner2PositionIndex, 2) ||
                        this->VertexIsOnLowPlane(filanetprob,
                                                 uniquePartner2PositionIndex, 2)};

                // 1 1 1 1
                if ((!true && onPlanesPartner1[0] && onPlanesPartner2[0]) ||
                    (!true && onPlanesPartner1[1] && onPlanesPartner2[1]) ||
                    (!true && onPlanesPartner1[2] && onPlanesPartner2[2])) // if(onPlanesPartner1 >= (1) &&
                                                                           // vertexIsUnique && edgesCreated ==
                                                                           // 0)
                {
                  if (vertexPartnerIsUnique)
                  {
                    // remove 2
                    this->uniqueVertices_.erase(this->uniqueVertices_.begin() + uniquePartner2PositionIndex);
                    debug_lastUnique--;

                    // WARNING
                    if (uniquePartner2PositionIndex < uniquePartner1PositionIndex)
                      --uniquePartner1PositionIndex;
                  }
                  continue;
                }
                else
                {
                  this->uniqueVertexEdgePartners_.push_back(std::vector<unsigned int>{uniquePartner1PositionIndex,
                                                                                      uniquePartner2PositionIndex});
                  ++edgesCreated;
                }
              }

              uniqueVertexEdgePartners.push_back(std::vector<unsigned int>{
                  uniquePartner1PositionIndex, uniquePartner2PositionIndex});
            }

            if (((!true && onPlanesPartner1[0]) ||
                 (!true && onPlanesPartner1[1]) ||
                 (!true && onPlanesPartner1[2])) &&
                vertexIsUnique && edgesCreated == 0) // no partner2 depends on partner1?
            {
              // remove 1
              this->uniqueVertices_.erase(this->uniqueVertices_.begin() + uniquePartner1PositionIndex);
              --debug_lastUnique;
            }
          }

          // finalize statistical data for network characterization
          // for (uint32_t vtxOrder : vertexOrders)
          //   this->cellVertexOrders_[cellIndex].push_back(vtxOrder);
          // double cellRad_in = 0;
          // size_t proj_num = 0;
          // for (std::vector<uint32_t> edge : uniqueVertexEdgePartners)
          // {
          //   vec3 vtxP1_pos(this->uniqueVertices_[edge[0]]);
          //   vec3 vtxP2_pos(this->uniqueVertices_[edge[1]]);
          //   vec3 dir = vtxP2_pos - vtxP1_pos;
          //   double dir_abs = dir.length();
          //   //* projection *
          //   //           part
          //   //     loc  . `
          //   //       . `  | dist
          //   //     . ` cos |
          //   // p1 ---------------------------- p2
          //   //            x   dir
          //   vec3 part_pos(this->particlePositions_[cellIndex]);
          //   vec3 loc = part_pos - vtxP1_pos;

          //   double cos_abs = loc.dot(dir);
          //   vec3 cos = dir * (cos_abs / dir_abs);
          //   vec3 x = vtxP1_pos + cos;
          //   vec3 dist = part_pos - x;
          //   double dist_abs = dist.length();
          //   if ((cos_abs > 0) & (cos_abs < dir_abs)) // projection is inside
          //   {
          //     cellRad_in += dist_abs;
          //     ++proj_num;
          //   }
          // }
          // if (proj_num)
          //   cellRad_in /= proj_num;
          // else
          //   std::cout << " ! | Projections rejected. Outside of all "
          //                "edges.\nParticle pos: "
          //             << this->particlePositions_[cellIndex][0] << " "
          //             << this->particlePositions_[cellIndex][1] << " "
          //             << this->particlePositions_[cellIndex][2] << "\n";
          // this->cellRad_in_[cellIndex] = cellRad_in;
          // avgRad /= vertexCount;
          // this->cellRads_[cellIndex] = cellRads;
          // this->cellRadAvg_[cellIndex] = avgRad;
          // this->cellRadMax_[cellIndex] = maxRad;
          // this->cellRadMin_[cellIndex] = minRad;

          if (cellIndex >= 0.05 * count * cellCount - 1)
          {
            auto stop_voro = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_voro = stop_voro - start_voro;
            std::cout << "   " << elapsed_voro.count() / 60 << " minutes needed for " << std::flush;
            std::cout << "   " << static_cast<int>(0.05 * count * 100.0) << " %\r" << std::flush;
            ++count;
          }

          ++cellIndex;
        }
      } while (loop.inc());

    std::cout << std::endl;

    //! NEW shifting to add all filaments that are not present on both periodic
    //! boundaries
    // this->ShiftEdges(filanetprob, this->uniqueVertexEdgePartners_,
    // this->uniqueVertices_);
    //! this breaks calc of fiber vol frac

    auto stop_voro = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_voro = stop_voro - start_voro;
    std::cout << "\n   Computation of Voronoi is done now. It took " << elapsed_voro.count() / 60 << " minutes. \n";

    // remove double edges in periodic BC dimension
    std::cout << "\n2) Removing double edges. Current edge count: " << this->uniqueVertexEdgePartners_.size() << "\n";
    auto start_removing_doubles = std::chrono::high_resolution_clock::now();
    // now clean up
    node_to_edges_ = std::vector<std::vector<unsigned int>>(uniqueVertices_.size(), std::vector<unsigned int>());
    for (unsigned int i_edge = 0; i_edge < uniqueVertexEdgePartners_.size(); ++i_edge)
    {
      node_to_edges_[this->uniqueVertexEdgePartners_[i_edge][0]].push_back(i_edge);
      node_to_edges_[this->uniqueVertexEdgePartners_[i_edge][1]].push_back(i_edge);
    }

    //! Careful: creates dead nodes! (vertices still exist!)
    // if (this->applyPeriodicBCsPerDim_[0] or this->applyPeriodicBCsPerDim_[1] or this->applyPeriodicBCsPerDim_[2])
    this->RemoveDoubles(filanetprob);

    auto stop_removing_doubles = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_removing_doubles = stop_removing_doubles - start_removing_doubles;
    std::cout << "   Double edges are removed now. It took " << elapsed_removing_doubles.count() / 60 << " minutes. \n";

    std::cout << "\n3) Cleaning up ... " << std::endl;
    ;
    auto start_cleaning = std::chrono::high_resolution_clock::now();

    // now clean up
    node_to_edges_ = std::vector<std::vector<unsigned int>>(uniqueVertices_.size(), std::vector<unsigned int>());
    for (unsigned int i_edge = 0; i_edge < uniqueVertexEdgePartners_.size(); ++i_edge)
    {
      node_to_edges_[this->uniqueVertexEdgePartners_[i_edge][0]].push_back(i_edge);
      node_to_edges_[this->uniqueVertexEdgePartners_[i_edge][1]].push_back(i_edge);
    }

    // Compute Vertex order
    uniqueVertices_map_.clear();
    this->vtxs_shifted_ = this->uniqueVertices_;
    this->ShiftVertices(filanetprob, this->vtxs_shifted_);

    std::vector<unsigned int> vertices_with_order_1;
    std::vector<unsigned int> vertices_with_order_3;
    for (unsigned int i_node = 0; i_node < node_to_edges_.size(); ++i_node)
    {
      if (node_to_edges_[i_node].size() == 4)
        uniqueVertices_map_[i_node] = vtxs_shifted_[i_node];

      if (node_to_edges_[i_node].size() == 1)
        vertices_with_order_1.push_back(i_node);

      if (node_to_edges_[i_node].size() == 3)
        vertices_with_order_3.push_back(i_node);
    }

    // find each partner
    for (unsigned int i = 0; i < vertices_with_order_3.size(); ++i)
    {
      for (unsigned int j = 0; j < vertices_with_order_1.size(); ++j)
      {
        if (VerticesMatch(vtxs_shifted_[vertices_with_order_3[i]],
                          vtxs_shifted_[vertices_with_order_1[j]]))
        {
          if (uniqueVertexEdgePartners_[node_to_edges_[vertices_with_order_1[j]][0]][0] == vertices_with_order_1[j])
            uniqueVertexEdgePartners_[node_to_edges_[vertices_with_order_1[j]][0]][0] = vertices_with_order_3[i];
          else
            uniqueVertexEdgePartners_[node_to_edges_[vertices_with_order_1[j]][0]][1] = vertices_with_order_3[i];

          uniqueVertices_map_[vertices_with_order_3[i]] = vtxs_shifted_[vertices_with_order_3[i]];
        }
      }
    }

    node_to_edges_ = std::vector<std::vector<unsigned int>>(uniqueVertices_.size(), std::vector<unsigned int>());
    for (unsigned int i_edge = 0; i_edge < uniqueVertexEdgePartners_.size(); ++i_edge)
    {
      node_to_edges_[this->uniqueVertexEdgePartners_[i_edge][0]].push_back(i_edge);
      node_to_edges_[this->uniqueVertexEdgePartners_[i_edge][1]].push_back(i_edge);
    }

    // put all vertex ids in vector to ease random draw of vertex
    uniqueVertices_for_random_draw_.reserve(uniqueVertices_map_.size());
    for (auto const &iter : uniqueVertices_map_)
      uniqueVertices_for_random_draw_.push_back(iter.first);

    auto stop_cleaning = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapses_cleaning = stop_cleaning - start_cleaning;
    std::cout << "   Cleaning is done now. It took " << elapses_cleaning.count() / 60 << " minutes. \n";

    std::cout << "   Number of lines: " << this->uniqueVertexEdgePartners_.size() << "\n"
              << std::flush;
    std::cout << "   Number of nodes: " << this->uniqueVertices_for_random_draw_.size() << "\n"
              << std::flush;

    unsigned int num_nodes = this->uniqueVertices_map_.size();
    unsigned int num_lines = this->uniqueVertexEdgePartners_.size();

    // output vertex orders
    // std::ofstream vtxOrders_initial_file(this->outputFolderPath_ + this->fileName_ + "_vertex_orders_initial.txt");
    // vtxOrders_initial_file << "vertex_orders \n";
    // for (unsigned int vtxId = 0; vtxId < this->node_to_edges_.size(); ++vtxId)
    // {
    //   if (node_to_edges_[vtxId].size() == 0)
    //     continue;
    //   vtxOrders_initial_file << node_to_edges_[vtxId].size() << "\n";
    // }

    bool adapt_connectivity = true;
    if (adapt_connectivity)
    {
      //  if (this->vtxOrder_goal_) this->Thinning(filanetprob); // old code

      std::cout << "\n4) Adapting valency distribution " << std::endl;
      auto start_valency = std::chrono::high_resolution_clock::now();

      // Adapt valency distribution to collagen network according to Nan2018
      // "Realizations of highly heterogeneous collagen networks via stochastic reconstruction
      //  for micromechanical analysis of tumor cell invasion" figure 6
      std::uniform_int_distribution<> dis_rand_line(0, 3);
      std::vector<double> dir_1(3, 0.0);
      std::vector<double> dir_2(3, 0.0);

      unsigned int num_z_3 = std::floor(0.72 * num_nodes);
      unsigned int num_z_4 = std::floor(0.2 * num_nodes);
      unsigned int num_z_5 = std::floor(0.054 * num_nodes);
      unsigned int num_z_6 = std::floor(0.011 * num_nodes);

      num_z_4 += num_nodes - (num_z_3 + num_z_4 + num_z_5 + num_z_6);

      // first take care of z = 6 (starting from all being z = 4)
      std::uniform_int_distribution<> rand_node(0, uniqueVertices_for_random_draw_.size() - 1);

      unsigned int num_curr_z_6 = 0;
      unsigned int num_curr_z_5 = 0;

      while (num_curr_z_6 < num_z_6)
      {
        unsigned int node_1 = uniqueVertices_for_random_draw_[rand_node(gen)];

        if (node_to_edges_[node_1].size() == 4)
        {
          for (int i = 0; i < 2; ++i)
          {
            bool success = false;

            unsigned int node_2 = 0;
            while (not success)
            {
              node_2 = uniqueVertices_for_random_draw_[rand_node(gen)];
              if (node_2 == node_1 or node_to_edges_[node_2].size() != 4)
                continue;

              // check distance
              for (unsigned int dim = 0; dim < 3; ++dim)
                dir_1[dim] = uniqueVertices_map_[node_1][dim] - uniqueVertices_map_[node_2][dim];

              UnShift3D(dir_1, dir_2);

              if (l2_norm(dir_1) < one_third * filanetprob->boxsize_[0])
              {
                success = true;
                break;
              }
            }

            // add line to these nodes
            std::vector<unsigned int> new_line(2, 0);
            new_line[0] = node_1;
            new_line[1] = node_2;
            node_to_edges_[node_1].push_back(uniqueVertexEdgePartners_.size());
            node_to_edges_[node_2].push_back(uniqueVertexEdgePartners_.size());
            uniqueVertexEdgePartners_.push_back(new_line);
            ++num_curr_z_5;
          }
          ++num_curr_z_6;
        }
      }

      // now take care of z = 5
      while (num_curr_z_5 < num_z_5)
      {
        unsigned int node_1 = uniqueVertices_for_random_draw_[rand_node(gen)];

        bool success = false;
        if (node_to_edges_[node_1].size() == 4)
        {
          unsigned int node_2 = 0;
          while (not success)
          {
            node_2 = uniqueVertices_for_random_draw_[rand_node(gen)];
            if (node_2 == node_1 or node_to_edges_[node_2].size() != 4)
              continue;

            // check distance
            for (unsigned int dim = 0; dim < 3; ++dim)
              dir_1[dim] = uniqueVertices_map_[node_1][dim] - uniqueVertices_map_[node_2][dim];

            UnShift3D(dir_1, dir_2);

            if (l2_norm(dir_1) < one_third * filanetprob->boxsize_[0])
            {
              success = true;
              break;
            }
          }

          // add line to these nodes
          std::vector<unsigned int> new_line(2, 0);
          new_line[0] = node_1;
          new_line[1] = node_2;
          node_to_edges_[node_1].push_back(uniqueVertexEdgePartners_.size());
          node_to_edges_[node_2].push_back(uniqueVertexEdgePartners_.size());
          uniqueVertexEdgePartners_.push_back(new_line);
          num_curr_z_5 += 2;
        }
      }

      // now do with z = 3
      unsigned int num_found = 0;
      unsigned int max_try = 100;
      std::vector<int> random_order = Permutation(uniqueVertices_for_random_draw_.size(), gen, dis_uni);
      // do twice for better results
      for (int s = 0; s < 2; ++s)
      {
        for (unsigned int rand_node_i = 0; rand_node_i < random_order.size(); ++rand_node_i)
        {
          int i_node = uniqueVertices_for_random_draw_[random_order[rand_node_i]];

          if (node_to_edges_[i_node].size() == 4)
          {
            unsigned int try_i = 1;
            bool success = false;
            unsigned int random_line = dis_rand_line(gen);
            unsigned int second_affected_node = -1;
            do
            {
              if (uniqueVertexEdgePartners_[node_to_edges_[i_node][random_line]][0] == i_node)
                second_affected_node = uniqueVertexEdgePartners_[node_to_edges_[i_node][random_line]][1];
              else
                second_affected_node = uniqueVertexEdgePartners_[node_to_edges_[i_node][random_line]][0];

              if (node_to_edges_[second_affected_node].size() == 4)
              {
                success = true;
                break;
              }

              ++try_i;

            } while (try_i < max_try);

            if (not success)
              continue;

            //! don't erase yet! this destroys the order in edgeIds_valid!
            this->uniqueVertexEdgePartners_[node_to_edges_[i_node][random_line]] =
                std::vector<unsigned int>{INT32_MAX, INT32_MAX};

            int index = std::distance(node_to_edges_[second_affected_node].begin(), std::find(node_to_edges_[second_affected_node].begin(),
                                                                                              node_to_edges_[second_affected_node].end(),
                                                                                              node_to_edges_[i_node][random_line]));

            node_to_edges_[second_affected_node].erase(node_to_edges_[second_affected_node].begin() + index);
            node_to_edges_[i_node].erase(node_to_edges_[i_node].begin() + random_line);

            num_found += 2;
          }

          if (num_found >= num_z_3)
            break;
        }
      }
      // now really remove edges
      // erase now
      this->uniqueVertexEdgePartners_.erase(std::remove(this->uniqueVertexEdgePartners_.begin(),
                                                        this->uniqueVertexEdgePartners_.end(),
                                                        std::vector<unsigned int>{INT32_MAX, INT32_MAX}),
                                            this->uniqueVertexEdgePartners_.end());
      num_lines = uniqueVertexEdgePartners_.size();

      // recompute node to edges after adaption of connectivity
      node_to_edges_.clear();
      node_to_edges_ = std::vector<std::vector<unsigned int>>(uniqueVertices_.size(), std::vector<unsigned int>());
      for (unsigned int i_edge = 0; i_edge < uniqueVertexEdgePartners_.size(); ++i_edge)
      {
        node_to_edges_[this->uniqueVertexEdgePartners_[i_edge][0]].push_back(i_edge);
        node_to_edges_[this->uniqueVertexEdgePartners_[i_edge][1]].push_back(i_edge);
      }

      auto stop_valency = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> elapsed_valency = stop_valency - start_valency;
      std::cout << "   Adaption of valency distribution is done now. It took " << elapsed_valency.count() / 60 << " minutes. \n";
    }

    // Compute Vertex order
    this->uniqueVertexOrders_.clear();
    for (unsigned int i_node = 0; i_node < node_to_edges_.size(); ++i_node)
    {
      this->uniqueVertexOrders_.push_back(node_to_edges_[i_node].size());
    }
    // std::cout << "   Average vertex order is " << CalcAvgVtxOrder() << std::endl;

    // statistical network data
    // this->networkCellRadAvg_ = 0;
    // this->networkCellRadAvg_in_ = 0;
    // this->networkCellRadMax_ = 0;
    // this->networkCellRadMin_ = 0;
    // for (unsigned int cellIndex = 0; cellIndex < cellCount; ++cellIndex)
    // {
    //   this->networkCellRadAvg_ += this->cellRadAvg_[cellIndex];
    //   this->networkCellRadAvg_in_ += this->cellRad_in_[cellIndex];
    //   this->networkCellRadMax_ += this->cellRadMax_[cellIndex];
    //   this->networkCellRadMin_ += this->cellRadMin_[cellIndex];
    // }

    // this->networkCellRadAvg_ /= cellCount;
    // this->networkCellRadAvg_in_ /= cellCount;
    // this->networkCellRadMax_ /= cellCount;
    // this->networkCellRadMin_ /= cellCount;

    // this->networkCellRadStdDev_ = 0;
    // double radiusDeltaSquared = 0;
    // for (unsigned int cellIndex = 0; cellIndex < cellCount; ++cellIndex)
    // {
    //   radiusDeltaSquared +=
    //       (this->cellRadAvg_[cellIndex] - this->networkCellRadAvg_) *
    //       (this->cellRadAvg_[cellIndex] - this->networkCellRadAvg_);
    // }

    // this->networkCellRadStdDev_ = std::sqrt(radiusDeltaSquared / (cellCount - 1));
    // this->networkCellRadStdDevNorm_ = this->networkCellRadStdDev_ / this->networkCellRadAvg_;

    // allocate memory for dnodes (finite element nodes of actual discretization)
    this->vertexNodeIds_ = std::vector<std::vector<unsigned int>>(this->uniqueVertices_.size());

    int numberOfFilaments = this->uniqueVertexEdgePartners_.size();
    filanetprob->numfil_ = numberOfFilaments;
    this->currnumfils_ = numberOfFilaments;

    //*************************************************************************************
    // DO SIMULATED ANNEALING
    bool do_simulated_annealing = true;
    if (not do_simulated_annealing)
      return;
    //*************************************************************************************

    std::cout << "\n5) Starting Simulated Annealing\n\n\n";
    // time measurement start
    auto start = std::chrono::high_resolution_clock::now();

    // Choose values for input parameters
    unsigned const int max_iter = 5000;
    unsigned const int max_subiter = 100;
    double weight_line = 1.0;
    double weight_cosine = 1.0;
    const double tolerance = 0.01;
    double temperature_inital = 0.05;
    double decay_rate_temperature = 0.95;
    double max_movement = 0.05 * filanetprob->boxsize_[0];
    unsigned int screen_output_every = 1000;

    // for binning
    unsigned int p_num_bins_lengths = 1000;
    unsigned int p_num_bins_cosines = 1000;

    num_nodes = uniqueVertices_map_.size();
    num_lines = uniqueVertexEdgePartners_.size();

    // ... some more
    // to select interchange movement
    std::uniform_int_distribution<> dis_action(1, 2);
    // to select random node
    std::uniform_int_distribution<> dis_node(0, num_nodes - 1);
    // to select random line
    std::uniform_int_distribution<> dis_line(0, num_lines - 1);
    // to select random node movement
    std::uniform_real_distribution<> dis_node_move(-1, 1);

    // ... and even more
    std::vector<double> rand_new_node_pos(3, 0.0);
    std::vector<std::vector<double>> uniqueVertices_backup(this->uniqueVertices_);
    std::vector<std::vector<unsigned int>> uniqueVertexEdgePartners_backup(this->uniqueVertexEdgePartners_);
    unsigned int random_line_1 = 0;
    unsigned int random_line_2 = 0;
    unsigned int iter = 0;
    double curr_energy_line = 0.0;
    double last_energy_line = 0.0;
    double curr_energy_cosine = 0.0;
    double last_energy_cosine = 0.0;
    double temperature = temperature_inital;
    double delta_energy = 0.0;
    // normalize lengths according to Lindström
    double length_norm_fac = 1.0 / std::pow((num_nodes / (filanetprob->boxsize_[0] * filanetprob->boxsize_[1] * filanetprob->boxsize_[2])), -1.0 / 3.0);

    // for binning
    std::vector<unsigned int> m_j_lengths;
    std::vector<unsigned int> m_j_cosines;
    double interval_size_lengths = 5.0 / p_num_bins_lengths;
    double interval_size_cosines = 2.0 / p_num_bins_cosines;

    // build uniqueVertices_to_Edge_map_
    for (unsigned int i_edge = 0; i_edge < uniqueVertexEdgePartners_.size(); ++i_edge)
    {
      uniqueVertices_to_Edge_map_[this->uniqueVertexEdgePartners_[i_edge][0]].push_back(i_edge);
      uniqueVertices_to_Edge_map_[this->uniqueVertexEdgePartners_[i_edge][1]].push_back(i_edge);
    }

    // compute cosine distribution
    std::vector<double> cosine_distribution(p_num_bins_cosines, 0.0);
    std::vector<std::vector<double>> node_cosine_to_bin(uniqueVertices_.size(), std::vector<double>());
    std::vector<double> dir_vec_1(3, 0.0);
    std::vector<double> dir_vec_2(3, 0.0);
    for (auto const &i_node : uniqueVertices_to_Edge_map_)
    {
      ComputeCosineDistributionOfNode(filanetprob, i_node.first, dir_vec_1, dir_vec_2,
                                      interval_size_cosines, node_cosine_to_bin, cosine_distribution);
    }
    std::vector<std::vector<double>> node_cosine_to_bin_backup(node_cosine_to_bin);

    unsigned int num_cosines = 0;
    for (unsigned int i_c = 0; i_c < cosine_distribution.size(); ++i_c)
      num_cosines += cosine_distribution[i_c];

    // compute length distribution
    std::vector<double> length_distribution(p_num_bins_lengths, 0.0);
    std::vector<double> edge_length_to_bin(uniqueVertexEdgePartners_.size(), -1.0);
    for (unsigned int i_edge = 0; i_edge < num_lines; ++i_edge)
    {
      UpdateLengthDistributionOfLine(filanetprob, i_edge, length_norm_fac, dir_vec_1,
                                     interval_size_lengths, edge_length_to_bin, length_distribution);
    }
    std::vector<double> edge_length_to_bin_backup(edge_length_to_bin);

    // compute for first iteration
    EnergyLineLindstrom(last_energy_line, num_lines, interval_size_lengths, length_distribution);
    EnergyCosineLindstrom(last_energy_cosine, interval_size_cosines, cosine_distribution, num_cosines);

    // write initial distributions
    // output initial filament lengths
    std::ofstream filLen_file_initial(this->outputFolderPath_ + this->fileName_ + "_fil_lengths_initial.txt");
    filLen_file_initial << "fil_lengths\n";
    for (unsigned int filId = 0; filId < this->uniqueVertexEdgePartners_.size(); ++filId)
      filLen_file_initial << this->GetFilamentLength(filId) * length_norm_fac << "\n";

    // print final cosine distribution
    std::ofstream filcoshisto_initial_file(this->outputFolderPath_ + this->fileName_ + "_cosine_histo_initial.txt");
    filcoshisto_initial_file << "cosine\n";
    for (unsigned int i_c = 0; i_c < cosine_distribution.size(); ++i_c)
      for (unsigned int j_c = 0; j_c < cosine_distribution[i_c]; ++j_c)
        filcoshisto_initial_file << interval_size_cosines * i_c + interval_size_cosines * 0.5 - 1.0 << "\n";

    // write temperature and energies to file
    std::ofstream fil_obj_function(this->outputFolderPath_ + this->fileName_ + "_obj_function.txt");
    fil_obj_function << "step, temperature, length, cosine, total \n";

    //---------------------------
    // START SIMULATED ANNEALING
    //---------------------------
    do
    {
      unsigned int action = 1; //dis_action(gen);

      // ************************************************
      // type one: move random point in random direction
      // ************************************************
      if (action == 1)
      {
        bool success = true;
        unsigned int subiter = 0;
        do
        {
          ++subiter;
          success = true;
          // select a random node
          unsigned int rand_node_id = uniqueVertices_for_random_draw_[dis_node(gen)];

          // update position of this vertex
          for (unsigned int idim = 0; idim < 3; ++idim)
          {
            this->uniqueVertices_[rand_node_id][idim] += dis_node_move(gen) * max_movement;
          }

          // recompute length and cosine distribution of affected nodes
          std::set<unsigned int> affected_nodes;
          std::set<unsigned int> affected_lines;
          for (auto const &iter_edges : node_to_edges_[rand_node_id])
          {
            affected_nodes.insert(this->uniqueVertexEdgePartners_[iter_edges][0]);
            affected_nodes.insert(this->uniqueVertexEdgePartners_[iter_edges][1]);
            affected_lines.insert(iter_edges);

            // check if line is longer than 1/3 of boxlength (we do not want this to
            // ensure that our RVE stays representative)
            if (GetEdgeLength(iter_edges) / length_norm_fac > one_third * filanetprob->boxsize_[0])
            {
              success = false;
              break;
            }
          }

          if (success == false)
          {
            RevertUpdateOfNodes(affected_nodes, uniqueVertices_backup);
            continue;
          }

          for (auto const &iter_nodes : affected_nodes)
            ComputeCosineDistributionOfNode(filanetprob, iter_nodes, dir_vec_1, dir_vec_2,
                                            interval_size_cosines, node_cosine_to_bin, cosine_distribution);

          for (auto const &iter_edges : affected_lines)
            UpdateLengthDistributionOfLine(filanetprob, iter_edges, length_norm_fac, dir_vec_1,
                                           interval_size_lengths, edge_length_to_bin, length_distribution);

          // compute energies
          // 1.) line
          EnergyLineLindstrom(curr_energy_line, num_lines, interval_size_lengths, length_distribution);

          // 2.) cosine
          EnergyCosineLindstrom(curr_energy_cosine, interval_size_cosines, cosine_distribution, num_cosines);

          // compute delta E
          delta_energy = weight_line * (curr_energy_line - last_energy_line) +
                         weight_cosine * (curr_energy_cosine - last_energy_cosine);

          if ((delta_energy < 0.0) or (dis_uni(gen) < std::exp(-delta_energy / temperature)))
          {
            last_energy_line = curr_energy_line;
            last_energy_cosine = curr_energy_cosine;
            UpdateBackUpOfNodes(affected_nodes, uniqueVertices_backup);
            UpdateBackupOfCosineDistribution(affected_nodes, node_cosine_to_bin, node_cosine_to_bin_backup);
            UpdateBackupOfLineDistribution(affected_lines, edge_length_to_bin, edge_length_to_bin_backup);
            success = true;
          }
          else
          {
            RevertUpdateOfNodes(affected_nodes, uniqueVertices_backup);
            RevertComputeCosineDistributionOfNode(affected_nodes, cosine_distribution, node_cosine_to_bin, node_cosine_to_bin_backup);
            RevertUpdateLengthDistributionOfLine(affected_lines, length_distribution, edge_length_to_bin, edge_length_to_bin_backup);
            success = false;
          }
        } while ((success == false) and (subiter < max_subiter));

        if (iter % screen_output_every == 0)
        {
          std::cout << "line energy move 1 " << curr_energy_line << std::endl;
          std::cout << "cosine energy move 1 " << curr_energy_cosine << std::endl;
          std::cout << " iter " << iter << std::endl;

          fil_obj_function << iter;
          fil_obj_function << ", " << temperature;
          fil_obj_function << ", " << curr_energy_line;
          fil_obj_function << ", " << curr_energy_cosine;
          fil_obj_function << ", " << curr_energy_line + curr_energy_cosine << "\n";
        }
      }

      // ************************************************
      // type two: change connection of two lines
      // ************************************************
      else if (action == 2)
      {

        //      Current Conf.           Case_1             Case_2
        //      _____________       _____________      ______________
        //
        //      1 o------o 2         1 o     o 2        1 o       o 2
        //                              \   /             |       |
        //                               \ /              |       |
        //                                /               |       |
        //                               /  \             |       |
        //      3 o------o 4         3  o    o 4        3 o       o 4
        //
        //%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

        bool success = false;
        unsigned int subiter = 0;
        do
        {
          ++subiter;
          success = true;

          // select two (different) random lines
          random_line_1 = dis_line(gen);
          bool not_yet_found = true;
          std::vector<unsigned int> nodes_line_1(2, 0);
          std::vector<unsigned int> nodes_line_2(2, 0);

          nodes_line_1[0] = uniqueVertexEdgePartners_[random_line_1][0];
          nodes_line_1[1] = uniqueVertexEdgePartners_[random_line_1][1];

          unsigned int control_iter = 0;
          while (not_yet_found)
          {
            ++control_iter;
            // prevent code from getting stuck in this loop
            if (control_iter > 1e06)
            {
              std::cout << " Not able to find close enough second edge for option 2 " << std::endl;
              exit(0);
            }

            random_line_2 = dis_line(gen);
            nodes_line_2[0] = uniqueVertexEdgePartners_[random_line_2][0];
            nodes_line_2[1] = uniqueVertexEdgePartners_[random_line_2][1];

            // already check distance here
            bool to_far_away = false;
            for (unsigned int j = 0; j < 2; ++j)
            {
              if (l2_norm_dist_two_points(filanetprob, uniqueVertices_[nodes_line_1[j]], uniqueVertices_[nodes_line_2[0]]) > one_third * filanetprob->boxsize_[0] or
                  l2_norm_dist_two_points(filanetprob, uniqueVertices_[nodes_line_1[j]], uniqueVertices_[nodes_line_2[1]]) > one_third * filanetprob->boxsize_[0])
              {
                to_far_away = true;
                break;
              }
            }

            if (to_far_away)
              continue;

            // check if lines share a node, if so, choose another one
            if (not((nodes_line_1[0] == nodes_line_2[0]) or (nodes_line_1[0] == nodes_line_2[1]) or
                    (nodes_line_1[1] == nodes_line_2[0]) or (nodes_line_1[1] == nodes_line_2[1])))
              not_yet_found = false;
          }

          std::set<unsigned int> affected_nodes;
          affected_nodes.insert(uniqueVertexEdgePartners_[random_line_1][0]);
          affected_nodes.insert(uniqueVertexEdgePartners_[random_line_1][1]);
          affected_nodes.insert(uniqueVertexEdgePartners_[random_line_2][0]);
          affected_nodes.insert(uniqueVertexEdgePartners_[random_line_2][1]);

          std::set<unsigned int> affected_lines;
          affected_lines.insert(random_line_1);
          affected_lines.insert(random_line_2);

          // get all nodes that are connected to the respective four nodes
          std::vector<std::set<unsigned int>> nodes_to_nodes_1(2, std::set<unsigned int>());
          std::vector<std::set<unsigned int>> nodes_to_nodes_2(2, std::set<unsigned int>());

          for (unsigned int j = 0; j < 2; ++j)
          {
            for (unsigned int k = 0; k < node_to_edges_[nodes_line_1[j]].size(); ++k)
            {
              nodes_to_nodes_1[j].insert(uniqueVertexEdgePartners_[k][0]);
              nodes_to_nodes_1[j].insert(uniqueVertexEdgePartners_[k][1]);
            }
          }

          for (unsigned int j = 0; j < 2; ++j)
          {
            for (unsigned int k = 0; k < node_to_edges_[nodes_line_2[j]].size(); ++k)
            {
              nodes_to_nodes_2[j].insert(uniqueVertexEdgePartners_[k][0]);
              nodes_to_nodes_2[j].insert(uniqueVertexEdgePartners_[k][1]);
            }
          }

          // decide which case is attempted first
          //        int mov_case = dis_action(gen);

          // we always try movement one first
          bool move_one_sucess = true;

          // next, check if one of the two new lines already exists for case 1
          if ((nodes_to_nodes_1[0].count(nodes_line_2[1])) or (nodes_to_nodes_1[1].count(nodes_line_2[0])))
            move_one_sucess = false;

          // update new connectivity
          if (move_one_sucess == true)
          {
            this->uniqueVertexEdgePartners_[random_line_1][1] = nodes_line_2[1];
            this->uniqueVertexEdgePartners_[random_line_2][1] = nodes_line_1[1];
          }

          if (move_one_sucess == false)
          {
            // check if one of the two new lines already exists for case 2
            if (nodes_to_nodes_1[0].count(nodes_line_2[0]) or nodes_to_nodes_1[1].count(nodes_line_2[1]))
            {
              success = false;
              continue;
            }

            // update new connectivity
            this->uniqueVertexEdgePartners_[random_line_1][1] = nodes_line_2[0];
            this->uniqueVertexEdgePartners_[random_line_2][0] = nodes_line_1[1];
          }

          // update length distribution
          UpdateLengthDistributionOfLine(filanetprob, random_line_1, length_norm_fac, dir_vec_1,
                                         interval_size_lengths, edge_length_to_bin, length_distribution);
          UpdateLengthDistributionOfLine(filanetprob, random_line_2, length_norm_fac, dir_vec_1,
                                         interval_size_lengths, edge_length_to_bin, length_distribution);
          // recompute cosine distribution of affected nodes
          for (unsigned int j = 0; j < 2; ++j)
          {
            ComputeCosineDistributionOfNode(filanetprob, nodes_line_1[j], dir_vec_1, dir_vec_2,
                                            interval_size_cosines, node_cosine_to_bin, cosine_distribution);
            ComputeCosineDistributionOfNode(filanetprob, nodes_line_2[j], dir_vec_1, dir_vec_2,
                                            interval_size_cosines, node_cosine_to_bin, cosine_distribution);
          }

          // compute energies
          // 1.) line
          EnergyLineLindstrom(curr_energy_line, num_lines, interval_size_lengths, length_distribution);

          // 2.) cosine
          EnergyCosineLindstrom(last_energy_cosine, interval_size_cosines, cosine_distribution, num_cosines);

          // compute delta E
          delta_energy = weight_line * (curr_energy_line - last_energy_line) +
                         weight_cosine * (curr_energy_cosine - last_energy_cosine);

          if ((delta_energy < 0.0) or (dis_uni(gen) < std::exp(-delta_energy / temperature)))
          {
            last_energy_line = curr_energy_line;
            last_energy_cosine = curr_energy_cosine;
            UpdateBackUpOfEdges(affected_lines, uniqueVertexEdgePartners_backup);
            UpdateBackupOfCosineDistribution(affected_nodes, node_cosine_to_bin, node_cosine_to_bin_backup);
            UpdateBackupOfLineDistribution(affected_lines, edge_length_to_bin, edge_length_to_bin_backup);
            success = true;
          }
          else
          {
            RevertUpdateOfEdges(affected_lines, uniqueVertexEdgePartners_backup);
            RevertComputeCosineDistributionOfNode(affected_nodes, cosine_distribution, node_cosine_to_bin, node_cosine_to_bin_backup);
            RevertUpdateLengthDistributionOfLine(affected_lines, length_distribution, edge_length_to_bin, edge_length_to_bin_backup);
            success = false;
          }
        } while (success == false and subiter < max_subiter);

        // screen output
        if (iter % screen_output_every == 0)
        {
          std::cout << "line energy move 2 " << curr_energy_line << std::endl;
          std::cout << "cosine energy move 2 " << curr_energy_cosine << std::endl;
          std::cout << " iter 2 " << iter << std::endl;
        }
      }

      // neither movement one nor two
      else
      {
        throw "You should not be here";
      }

      // according to Nan2018 (power law cooling schedule)
      if (iter % 1000 == 0)
      {
        temperature = std::pow(decay_rate_temperature, iter / 1000.0) * temperature_inital;
        std::cout << "temperature " << temperature << std::endl;
      }

      ++iter;
    } while ((iter < max_iter) and ((last_energy_line > tolerance) or (last_energy_cosine > tolerance)));

    // print final cosine distribution
    std::ofstream filcoshisto_file(this->outputFolderPath_ + this->fileName_ + "_cosine_histo.txt");
    filcoshisto_file << "cosine\n";
    for (unsigned int i_c = 0; i_c < cosine_distribution.size(); ++i_c)
      for (unsigned int j_c = 0; j_c < cosine_distribution[i_c]; ++j_c)
        filcoshisto_file << interval_size_cosines * i_c + interval_size_cosines * 0.5 - 1.0 << "\n";

    std::ofstream filcos_file(this->outputFolderPath_ + this->fileName_ + "_cosine_normal.txt");
    filcos_file << "bin, cosine \n";
    for (unsigned int i_c = 0; i_c < cosine_distribution.size(); ++i_c)
    {
      filcos_file << interval_size_cosines * i_c + interval_size_cosines * 0.5;
      filcos_file << ", " << cosine_distribution[i_c] - 1.0 << "\n";
    }

    // time measurement end
    auto stop = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = stop - start;

    std::cout << "\nSimulation annealing took " << elapsed.count() / 60 << " minutes for " << iter << " iterations" << std::endl;
    std::cout << "Final line energy:   " << last_energy_line << std::endl;
    std::cout << "Final cosine energy: " << last_energy_cosine << std::endl;

    // now that we are done shift one time again to provide the shifted coordinates
    // for other functions
    this->vtxs_shifted_ = this->uniqueVertices_;
    this->ShiftVertices(filanetprob, this->vtxs_shifted_);

    std::ofstream partnersOutFile("partners.out");
    for (auto partner : this->uniqueVertexEdgePartners_)
    {
      partnersOutFile << partner[0] << " "
                      << partner[1] << std::endl;
    }
    partnersOutFile << std::endl;
    partnersOutFile.close();
    std::ofstream verticesOutFile("vertices.out");
    for (auto vertex : this->uniqueVertices_)
    {
      verticesOutFile << vertex[0] << " "
                      << vertex[1] << " "
                      << vertex[2] << std::endl;
    }
    verticesOutFile << std::endl;
    verticesOutFile.close();

    boost::filesystem::ifstream partnersInFile("partners.out");
    std::string line;
    std::vector<std::vector<int>> partners;
    while (std::getline(partnersInFile, line))
    {
      if (line != "")
      {
        std::vector<std::string> parts;
        boost::split(parts, line, boost::is_any_of(" "));
        std::vector<int> partner = {std::stoi(parts[0]), std::stoi(parts[1])};
        partners.push_back(partner);
      }
      else
        break;
    }
    partnersInFile.close();

    boost::filesystem::ifstream verticesInFile("vertices.out");
    std::vector<std::vector<double>> vs;
    while (std::getline(verticesInFile, line))
    {
      if (line != "")
      {
        std::vector<std::string> parts;
        boost::split(parts, line, boost::is_any_of(" "));
        std::vector<double> vertex = {std::stod(parts[0]), std::stod(parts[1]), std::stod(parts[2])};
        vs.push_back(vertex);
      }
      else
        break;
    }
    verticesInFile.close();
  }

  double GetFilamentLength(
      unsigned int filamentIndex) const
  {
    return this->GetEdgeLength(filamentIndex);
  }

  double GetEdgeLength(unsigned int edgeUId) const
  {
    std::vector<unsigned int> partners = this->uniqueVertexEdgePartners_[edgeUId];
    std::vector<double> partner1Position = this->uniqueVertices_[partners[0]];
    std::vector<double> partner2Position = this->uniqueVertices_[partners[1]];

    UnShift3D(partner1Position, partner2Position);

    double deltaX = partner2Position[0] - partner1Position[0];
    double deltaY = partner2Position[1] - partner1Position[1];
    double deltaZ = partner2Position[2] - partner1Position[2];
    double squareX = deltaX * deltaX;
    double squareY = deltaY * deltaY;
    double squareZ = deltaZ * deltaZ;
    return std::sqrt(squareX + squareY + squareZ);
  }

  bool VertexIsOnHighPlane(
      FilamentNetworkProblem *filanetprob, unsigned int vertexUId,
      unsigned int dimension) const
  {
    return this->PointIsOnHighPlane(filanetprob, this->uniqueVertices_[vertexUId],
                                    dimension);
  }

  bool VertexIsOnLowPlane(
      FilamentNetworkProblem *filanetprob, unsigned int vertexUId,
      unsigned int dimension) const
  {
    return this->PointIsOnLowPlane(filanetprob, this->uniqueVertices_[vertexUId],
                                   dimension);
  }

  bool PointIsOverHighPlane(
      FilamentNetworkProblem *filanetprob, std::vector<double> const &point,
      unsigned int dimension) const
  {
    return (filanetprob->boxZeroPoint_[dimension] +
            filanetprob->boxsize_[dimension] / 2) <= point[dimension];
  }

  bool PointIsOverLowPlane(
      FilamentNetworkProblem *filanetprob,
      std::vector<double> const &point,
      unsigned int dimension) const
  {
    return (filanetprob->boxZeroPoint_[dimension] -
            filanetprob->boxsize_[dimension] / 2) >= point[dimension];
  }

  bool PointIsOnHighPlane(
      FilamentNetworkProblem *filanetprob, std::vector<double> const &point,
      unsigned int dimension) const
  {
    double compareTolerance = 1e-13;
    return std::abs(filanetprob->boxZeroPoint_[dimension] +
                    filanetprob->boxsize_[dimension] / 2 - point[dimension]) <
           compareTolerance;
  }

  bool PointIsOnLowPlane(
      FilamentNetworkProblem *filanetprob, std::vector<double> const &point,
      unsigned int dimension) const
  {
    double compareTolerance = 1e-13;
    return std::abs(filanetprob->boxZeroPoint_[dimension] -
                    filanetprob->boxsize_[dimension] / 2 - point[dimension]) <
           compareTolerance;
  }

  bool VerticesMatch(
      std::vector<double> const &vertex1,
      std::vector<double> const &vertex2) const
  {
    double compareTolerance = 1e-7;
    if (std::abs(vertex2[0] - vertex1[0]) > compareTolerance)
      return false;
    if (std::abs(vertex2[1] - vertex1[1]) > compareTolerance)
      return false;
    if (std::abs(vertex2[2] - vertex1[2]) > compareTolerance)
      return false;

    return true;
  }

  std::vector<unsigned int> ShiftVertices(
      FilamentNetworkProblem *filanetprob,
      std::vector<std::vector<double>> &vertices) const
  {
    std::vector<unsigned int> shifted_lines;
    shifted_lines.reserve(static_cast<int>(vertices.size() * 0.2));
    for (unsigned int i = 0; i < vertices.size(); ++i)
    {
      for (auto dim : {0, 1, 2})
      {
        if (!true)
          continue;

        if (this->PointIsOverHighPlane(filanetprob, vertices[i], dim))
        {
          this->ShiftPointDown(vertices[i], filanetprob->boxsize_, dim);
          for (unsigned int k = 0; k < node_to_edges_[i].size(); ++k)
            shifted_lines.push_back(node_to_edges_[i][k]);
        }

        else if (this->PointIsOverLowPlane(filanetprob, vertices[i], dim))
        {
          this->ShiftPointUp(vertices[i], filanetprob->boxsize_, dim);

          for (unsigned int k = 0; k < node_to_edges_[i].size(); ++k)
            shifted_lines.push_back(node_to_edges_[i][k]);
        }
      }
    }
    return shifted_lines;
  }

  void ShiftPointDown(std::vector<double> &point,
                      std::vector<double> const &boxSize,
                      unsigned int dim) const
  {
    point[dim] -= boxSize[dim];
  }

  void ShiftPointUp(std::vector<double> &point,
                    std::vector<double> const &boxSize,
                    unsigned int dim) const
  {
    point[dim] += boxSize[dim];
  }

  bool UnShift1D(
      int dim, double &d, double const &ref, double const &X) const
  {
    bool unshifted = false;

    if (not true)
      return unshifted;

    double x = d + X;

    if (x - ref < -0.5 * boxsize_[dim])
    {
      unshifted = true;
      d += boxsize_[dim];
    }
    else if (x - ref > 0.5 * boxsize_[dim])
    {
      unshifted = true;
      d -= boxsize_[dim];
    }

    return unshifted;
  }

  void UnShift3D(
      std::vector<double> &d, std::vector<double> const &ref, std::vector<double> const X = std::vector<double>(3, 0.0)) const
  {
    for (int dim = 0; dim < 3; ++dim)
      UnShift1D(dim, d[dim], ref[dim], X[dim]);
  }

  void get_unshifted_dir_vec(FilamentNetworkProblem *filanetprob,
                             std::vector<double> x_1, std::vector<double> const &x_2, std::vector<double> &dirvec) const
  {
    UnShift3D(x_1, x_2);

    for (int idim = 0; idim < 3; ++idim)
      dirvec[idim] = x_1[idim] - x_2[idim];
  };

  std::vector<int> Permutation(int number,
                                                      std::mt19937 &gen,
                                                      std::uniform_real_distribution<> &dis_uni) const
  {
    // auxiliary variable
    int j = 0;

    // result vector initialized with ordered numbers from 0 to N-1
    std::vector<int> randorder(number, 0);
    for (int i = 0; i < (int)randorder.size(); i++)
      randorder[i] = i;

    for (int i = 0; i < number; ++i)
    {
      // generate random number between 0 and i
      j = (int)std::floor((i + 1.0) * dis_uni(gen));

      /*exchange values at positions i and j (note: value at position i is i due to above
     *initialization and because so far only positions <=i have been changed*/
      randorder[i] = randorder[j];
      randorder[j] = i;
    }

    return randorder;
  }

  double l2_norm(std::vector<double> const &u) const
  {
    double accum = 0.;
    for (int i = 0; i < u.size(); ++i)
    {
      accum += u[i] * u[i];
    }
    return sqrt(accum);
  }
  double l2_norm_dist_two_points(FilamentNetworkProblem *filanetprob,
                                 std::vector<double> x_1, std::vector<double> const &x_2) const
  {
    UnShift3D(x_1, x_2);
    std::vector<double> dirvec(3, 0.0);

    for (int idim = 0; idim < 3; ++idim)
      dirvec[idim] = x_1[idim] - x_2[idim];

    return l2_norm(dirvec);
  }

  void ComputeCosineDistributionOfNode(
      FilamentNetworkProblem *filanetprob,
      const unsigned int i_node,
      std::vector<double> &dir_vec_1,
      std::vector<double> &dir_vec_2,
      double interval_size_cosines,
      std::vector<std::vector<double>> &node_cosine_to_bin,
      std::vector<double> &cosine_distribution)
  {
    // undo old
    for (unsigned int p = 0; p < node_cosine_to_bin[i_node].size(); ++p)
      --cosine_distribution[node_cosine_to_bin[i_node][p]];

    node_cosine_to_bin[i_node].clear();

    // loop over all edges of the respective node
    for (unsigned int i = 0; i < uniqueVertices_to_Edge_map_[i_node].size(); ++i)
    {
      // edge 1
      unsigned int edge_1 = uniqueVertices_to_Edge_map_[i_node][i];

      // get direction vector
      for (unsigned int idim = 0; idim < 3; ++idim)
      {
        if (this->uniqueVertexEdgePartners_[edge_1][0] == i_node)
          get_unshifted_dir_vec(filanetprob, uniqueVertices_[uniqueVertexEdgePartners_[edge_1][1]],
                                uniqueVertices_[uniqueVertexEdgePartners_[edge_1][0]], dir_vec_1);
        else
          get_unshifted_dir_vec(filanetprob, uniqueVertices_[uniqueVertexEdgePartners_[edge_1][0]],
                                uniqueVertices_[uniqueVertexEdgePartners_[edge_1][1]], dir_vec_1);
      }

      for (unsigned int j = i + 1; j < uniqueVertices_to_Edge_map_[i_node].size(); ++j)
      {
        // edge 2
        unsigned int edge_2 = uniqueVertices_to_Edge_map_[i_node][j];

        // get direction vector
        for (unsigned int idim = 0; idim < 3; ++idim)
        {
          if (this->uniqueVertexEdgePartners_[edge_2][0] == i_node)
            get_unshifted_dir_vec(filanetprob, uniqueVertices_[uniqueVertexEdgePartners_[edge_2][1]],
                                  uniqueVertices_[uniqueVertexEdgePartners_[edge_2][0]], dir_vec_2);
          else
            get_unshifted_dir_vec(filanetprob, uniqueVertices_[uniqueVertexEdgePartners_[edge_2][0]],
                                  uniqueVertices_[uniqueVertexEdgePartners_[edge_2][1]], dir_vec_2);
        }

        // compute cosine
        double curr_cosine = std::inner_product(std::begin(dir_vec_1), std::end(dir_vec_1), std::begin(dir_vec_2), 0.0);
        curr_cosine /= l2_norm(dir_vec_1) * l2_norm(dir_vec_2);

        // add cosine
        unsigned int bin = std::floor((curr_cosine + 1.0) / interval_size_cosines);
        bin = (bin >= cosine_distribution.size()) ? (cosine_distribution.size() - 1) : bin;
        ++cosine_distribution[bin];
        node_cosine_to_bin[i_node].push_back(bin);
      }
    }
  }

  /*----------------------------------------------------------------------*
 *----------------------------------------------------------------------*/
  void UpdateLengthDistributionOfLine(
      FilamentNetworkProblem *filanetprob,
      const unsigned int i_edge,
      double length_norm_fac,
      std::vector<double> &dir_vec_1,
      double interval_size_lengths,
      std::vector<double> &edge_length_to_bin,
      std::vector<double> &length_distribution) const
  {
    if (edge_length_to_bin[i_edge] > -0.1)
      --length_distribution[edge_length_to_bin[i_edge]];

    unsigned int node_1 = this->uniqueVertexEdgePartners_[i_edge][0];
    unsigned int node_2 = this->uniqueVertexEdgePartners_[i_edge][1];

    double curr_new_length = l2_norm_dist_two_points(filanetprob, this->uniqueVertices_[node_1],
                                                     this->uniqueVertices_[node_2]) *
                             length_norm_fac;

    unsigned int curr_bin = std::floor(curr_new_length / interval_size_lengths);
    curr_bin = curr_bin >= (length_distribution.size()) ? (length_distribution.size() - 1) : curr_bin;
    edge_length_to_bin[i_edge] = curr_bin;
    ++length_distribution[curr_bin];
  }

  /*----------------------------------------------------------------------*
 *----------------------------------------------------------------------*/
  void RevertUpdateLengthDistributionOfLine(
      std::set<unsigned int> const &edges_to_revert,
      std::vector<double> &length_distribution,
      std::vector<double> &edge_length_to_bin,
      std::vector<double> const &edge_length_to_bin_backup) const
  {
    for (auto const &i_edge : edges_to_revert)
    {
      --length_distribution[edge_length_to_bin[i_edge]];
      ++length_distribution[edge_length_to_bin_backup[i_edge]];

      edge_length_to_bin[i_edge] = edge_length_to_bin_backup[i_edge];
    }
  }

  void UpdateBackupOfCosineDistribution(
      std::set<unsigned int> const &nodes_to_revert,
      std::vector<std::vector<double>> const &node_cosine_to_bin,
      std::vector<std::vector<double>> &node_cosine_to_bin_backup) const
  {
    for (auto const &i_node : nodes_to_revert)
    {
      node_cosine_to_bin_backup[i_node] = node_cosine_to_bin[i_node];
    }
  }

  /*----------------------------------------------------------------------*
 *----------------------------------------------------------------------*/
  void RevertUpdateOfNodes(
      std::set<unsigned int> const &nodes_to_revert,
      std::vector<std::vector<double>> const &nodes_backup)
  {
    for (auto const &i_node : nodes_to_revert)
    {
      this->uniqueVertices_[i_node] = nodes_backup[i_node];
    }
  }

  /*----------------------------------------------------------------------*
 *----------------------------------------------------------------------*/
  void UpdateBackUpOfNodes(
      std::set<unsigned int> const &nodes_to_revert,
      std::vector<std::vector<double>> &nodes_backup)
  {
    for (auto const &i_node : nodes_to_revert)
    {
      nodes_backup[i_node] = this->uniqueVertices_[i_node];
    }
  }

  /*----------------------------------------------------------------------*
 *----------------------------------------------------------------------*/
  void RevertUpdateOfEdges(
      std::set<unsigned int> const &edges_to_revert,
      std::vector<std::vector<unsigned int>> const &uniqueVertexEdgePartners_backup)
  {
    for (auto const &i_edge : edges_to_revert)
    {
      uniqueVertexEdgePartners_[i_edge] = uniqueVertexEdgePartners_backup[i_edge];
    }
  }

  /*----------------------------------------------------------------------*
 *----------------------------------------------------------------------*/
  void UpdateBackUpOfEdges(
      std::set<unsigned int> const &edges_to_revert,
      std::vector<std::vector<unsigned int>> &uniqueVertexEdgePartners_backup)
  {
    for (auto const &i_edge : edges_to_revert)
    {
      uniqueVertexEdgePartners_backup[i_edge] = uniqueVertexEdgePartners_[i_edge];
    }
  }

  void UpdateBackupOfLineDistribution(
      std::set<unsigned int> const &edges_to_revert,
      std::vector<double> const &edge_length_to_bin,
      std::vector<double> &edge_length_to_bin_backup) const
  {
    for (auto const &i_edge : edges_to_revert)
    {
      edge_length_to_bin_backup[i_edge] = edge_length_to_bin[i_edge];
    }
  }

  void RevertComputeCosineDistributionOfNode(
      std::set<unsigned int> const &nodes_to_revert,
      std::vector<double> &cosine_distribution,
      std::vector<std::vector<double>> &node_cosine_to_bin,
      std::vector<std::vector<double>> const &node_cosine_to_bin_backup) const
  {
    for (auto const &i_node : nodes_to_revert)
    {
      for (unsigned int i = 0; i < node_cosine_to_bin[i_node].size(); ++i)
      {
        --cosine_distribution[node_cosine_to_bin[i_node][i]];
      }

      for (unsigned int i = 0; i < node_cosine_to_bin_backup[i_node].size(); ++i)
      {
        ++cosine_distribution[node_cosine_to_bin_backup[i_node][i]];
      }

      node_cosine_to_bin[i_node] = node_cosine_to_bin_backup[i_node];
    }
  }

  /*----------------------------------------------------------------------*
 *----------------------------------------------------------------------*/
  void EnergyLineLindstrom(
      double &curr_energy_line,
      unsigned int num_lines,
      double interval_size_lengths,
      std::vector<double> const &length_distribution) const
  {
    curr_energy_line = 0.0;

    double static mue = -0.3;   // mean
    double static sigma = 0.68; // standard deviation

    double one_sixth = 1.0 / 6.0;
    double curr_x = 0.0;
    double F = 0.0;
    double M = 0.0;
    double S = 0.0;
    for (unsigned int p = 0; p < length_distribution.size(); ++p)
    {
      curr_x = interval_size_lengths * p + interval_size_lengths * 0.5;
      F = (0.5 + 0.5 * std::erf((std::log(curr_x) - mue) / (sigma * M_SQRT2)));
      if (p > 0)
        M += length_distribution[p - 1];
      S = M - num_lines * F - 0.5;
      curr_energy_line += length_distribution[p] * (one_sixth * (length_distribution[p] + 1.0) *
                                                        (6.0 * S + 2.0 * length_distribution[p] + 1.0) +
                                                    S * S);
    }
    curr_energy_line *= 1.0 / (num_lines * num_lines);
  }

  void EnergyCosineLindstrom(
      double &curr_energy_cosine,
      double interval_size_cosines,
      std::vector<double> const &cosine_distribution,
      unsigned int num_cosines) const
  {
    curr_energy_cosine = 0.0;

    double static b_1 = 0.646666666666667 / 2.0;
    double static b_2 = -0.126666666666667 / 4.0;
    double static b_3 = 0.0200000000000001 / 6.0;

    double one_sixth = 1.0 / 6.0;
    double power_2 = 0.0;
    double curr_x = 0.0;
    double F = 0.0;
    double M = 0.0;
    double S = 0.0;
    for (unsigned int p = 0; p < cosine_distribution.size(); ++p)
    {
      curr_x = interval_size_cosines * p + interval_size_cosines * 0.5 - 1.0;
      power_2 = (1.0 - curr_x) * (1.0 - curr_x);
      F = -1.0 * b_1 * power_2 - b_2 * power_2 * power_2 - b_3 * power_2 * power_2 * power_2 + 1.0;
      if (p > 0)
        M += cosine_distribution[p - 1];
      S = M - num_cosines * F - 0.5;
      curr_energy_cosine += cosine_distribution[p] * (one_sixth * (cosine_distribution[p] + 1.0) *
                                                          (6.0 * S + 2.0 * cosine_distribution[p] + 1.0) +
                                                      S * S);
    }

    curr_energy_cosine *= 1.0 / (num_cosines * num_cosines);
  }

  void RemoveDoubles(FilamentNetworkProblem *filanetprob)
  {
    auto start_remove = std::chrono::high_resolution_clock::now();
    std::vector<std::vector<double>> vtxs_shifted = this->uniqueVertices_;

    std::vector<unsigned int> shifted_lines = this->ShiftVertices(filanetprob, vtxs_shifted);

    //  // make unique
    //  std::set<int> s;
    //  unsigned size = shifted_lines.size();
    //  for( unsigned int k = 0; k < size; ++k ) s.insert( shifted_lines[k] );
    //  shifted_lines.assign( s.begin(), s.end() );
    //
    //  unsigned int count = 1;
    //  for (unsigned int i = 0; i < shifted_lines.size(); ++i)
    //  {
    //    if ((uniqueVertexEdgePartners_[shifted_lines[i]][0] == INT32_MAX) || (uniqueVertexEdgePartners_[shifted_lines[i]][1] == INT32_MAX))
    //    {
    //      continue;
    //    }
    //
    //    for (unsigned int j = 0; j < uniqueVertexEdgePartners_.size(); ++j)
    //    {
    //      if (shifted_lines[i] == j or
    //          (uniqueVertexEdgePartners_[j][0] == INT32_MAX) or (uniqueVertexEdgePartners_[j][1] == INT32_MAX))
    //      {
    //        continue;
    //      }
    //      if ((uniqueVertexEdgePartners_[shifted_lines[i]][0] == INT32_MAX) || (uniqueVertexEdgePartners_[shifted_lines[i]][1] == INT32_MAX))
    //      {
    //        continue;
    //      }
    //
    //      std::vector<double> edge1vtx1 = vtxs_shifted[uniqueVertexEdgePartners_[shifted_lines[i]][0]];
    //      std::vector<double> edge1vtx2 = vtxs_shifted[uniqueVertexEdgePartners_[shifted_lines[i]][1]];
    //      std::vector<double> edge2vtx1 = vtxs_shifted[uniqueVertexEdgePartners_[j][0]];
    //      std::vector<double> edge2vtx2 = vtxs_shifted[uniqueVertexEdgePartners_[j][1]];
    //
    //      if ((this->VerticesMatch(edge1vtx1, edge2vtx1) && this->VerticesMatch(edge1vtx2, edge2vtx2)) ||
    //          (this->VerticesMatch(edge1vtx1, edge2vtx2) && this->VerticesMatch(edge1vtx2, edge2vtx1)))
    //      {
    //        uniqueVertexEdgePartners_[shifted_lines[i] > j ? shifted_lines[i] : j ] = std::vector<unsigned int>{INT32_MAX, INT32_MAX};
    //      }
    //    }
    //
    //    if (i >= 0.05 * count * shifted_lines.size() - 1 )
    //    {
    //      auto stop_remove = std::chrono::high_resolution_clock::now();
    //      std::chrono::duration<double> elapsed_remove = stop_remove - start_remove;
    //      std::cout << "   " << elapsed_remove.count()/60 << " minutes needed for " << std::flush;
    //      std::cout <<"   " << static_cast<int>(0.05 * count * 100.0) << " %\r" << std::flush;
    //      ++count;
    //    }
    //  }

    size_t edge1Id = 0;
    unsigned int count = 1;
    for (auto &edge1 : this->uniqueVertexEdgePartners_)
    {
      if ((edge1[0] == INT32_MAX) || (edge1[1] == INT32_MAX))
      {
        ++edge1Id;
        continue;
      }

      size_t edge2Id = 0;
      for (auto &edge2 : this->uniqueVertexEdgePartners_)
      {
        if (edge1Id >= edge2Id)
        {
          ++edge2Id;
          continue;
        }

        if ((edge2[0] == INT32_MAX) || (edge2[1] == INT32_MAX))
        {
          ++edge2Id;
          continue;
        }

        std::vector<double> edge1vtx1 = vtxs_shifted[edge1[0]];
        std::vector<double> edge1vtx2 = vtxs_shifted[edge1[1]];
        std::vector<double> edge2vtx1 = vtxs_shifted[edge2[0]];
        std::vector<double> edge2vtx2 = vtxs_shifted[edge2[1]];

        if ((this->VerticesMatch(edge1vtx1, edge2vtx1) && this->VerticesMatch(edge1vtx2, edge2vtx2)) ||
            (this->VerticesMatch(edge1vtx1, edge2vtx2) && this->VerticesMatch(edge1vtx2, edge2vtx1)))
        {
          edge2 = std::vector<unsigned int>{INT32_MAX, INT32_MAX};
        }
        ++edge2Id;
      }

      if (edge1Id >= 0.05 * count * uniqueVertexEdgePartners_.size() - 1)
      {
        auto stop_remove = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed_remove = stop_remove - start_remove;
        std::cout << "   " << elapsed_remove.count() / 60 << " minutes needed for " << std::flush;
        std::cout << "   " << static_cast<int>(0.05 * count * 100.0) << " %\r" << std::flush;
        ++count;
      }

      ++edge1Id;
    }
    std::cout << std::endl;

    this->uniqueVertexEdgePartners_.erase(
        std::remove(this->uniqueVertexEdgePartners_.begin(),
                    this->uniqueVertexEdgePartners_.end(),
                    std::vector<unsigned int>{INT32_MAX, INT32_MAX}),
        this->uniqueVertexEdgePartners_.end());
  }
};