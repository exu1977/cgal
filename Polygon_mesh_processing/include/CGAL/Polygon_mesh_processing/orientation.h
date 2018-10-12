// Copyright (c) 2013 GeometryFactory (France).
// All rights reserved.
//
// This file is part of CGAL (www.cgal.org).
// You can redistribute it and/or modify it under the terms of the GNU
// General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// Licensees holding a valid commercial license may use this file in
// accordance with the commercial license agreement provided with the software.
//
// This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
// WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
//
// $URL$
// $Id$
// SPDX-License-Identifier: GPL-3.0+
//
//
// Author(s)     : Ilker O. Yaz


#ifndef CGAL_ORIENT_POLYGON_MESH_H
#define CGAL_ORIENT_POLYGON_MESH_H

#include <CGAL/license/Polygon_mesh_processing/orientation.h>


#include <algorithm>
#include <CGAL/Polygon_mesh_processing/connected_components.h>
#include <CGAL/Polygon_mesh_processing/compute_normal.h>
#include <CGAL/Polygon_mesh_processing/internal/named_function_params.h>
#include <CGAL/Polygon_mesh_processing/internal/named_params_helper.h>
#include <CGAL/Polygon_mesh_processing/self_intersections.h>
#include <CGAL/Side_of_triangle_mesh.h>
#include <CGAL/Projection_traits_xy_3.h>
#include <CGAL/boost/graph/helpers.h>
#include <CGAL/boost/graph/iterator.h>
#include <CGAL/utility.h>

#include <boost/foreach.hpp>
#include <boost/unordered_set.hpp>
#include <boost/dynamic_bitset.hpp>
namespace CGAL {

namespace Polygon_mesh_processing {

namespace internal{

  template <class GT, class VPmap>
  struct Compare_vertex_points_z_3
  {
    VPmap vpmap;
    typename GT::Compare_z_3 compare_z;

    Compare_vertex_points_z_3(VPmap const& vpmap, const GT& gt)
      : vpmap(vpmap)
      , compare_z(gt.compare_z_3_object())
    {}

    typedef bool result_type;
    template <class vertex_descriptor>
    bool operator()(vertex_descriptor v1, vertex_descriptor v2) const
    {
      return CGAL::SMALLER == compare_z(get(vpmap, v1), get(vpmap, v2));
    }
  };


  template<typename PolygonMesh, typename NamedParameters>
  bool is_outward_oriented(typename boost::graph_traits<PolygonMesh>::vertex_descriptor v_max,
                           const PolygonMesh& pmesh,
                           const NamedParameters& np)
  {
    using boost::choose_param;
    using boost::get_param;

    CGAL_assertion(halfedge(v_max, pmesh)!=boost::graph_traits<PolygonMesh>::null_halfedge());

    //VertexPointMap
    typedef typename GetVertexPointMap<PolygonMesh, NamedParameters>::const_type VPMap;
    VPMap vpmap = choose_param(get_param(np, vertex_point),
                               get_const_property_map(vertex_point, pmesh));
    //Kernel
    typedef typename GetGeomTraits<PolygonMesh, NamedParameters>::type GT;
    GT gt = choose_param(get_param(np, internal_np::geom_traits), GT());

    //among the incoming edges of `v_max`, find one edge `e` with the minimal slope
    typedef typename boost::graph_traits<PolygonMesh>::halfedge_descriptor halfedge_descriptor;
    halfedge_descriptor min_slope_he = halfedge(v_max, pmesh);
    CGAL_assertion(v_max == target(min_slope_he, pmesh));

    typename GT::Compare_slope_3 compare_slope = gt.compare_slope_3_object();
    BOOST_FOREACH(halfedge_descriptor he, halfedges_around_target(v_max, pmesh))
    {
      CGAL_assertion(v_max == target(min_slope_he, pmesh));
      CGAL_assertion(v_max == target(he, pmesh));

      if(CGAL::SMALLER == compare_slope(get(vpmap, source(he, pmesh)),
                                        get(vpmap, v_max),
                                        get(vpmap, source(min_slope_he, pmesh)),
                                        get(vpmap, v_max)))
      {
        min_slope_he = he;
      }
    }

    // We compute the orientations of the two triangles incident to the edge
    // of `min_slope_he` projected in the xy-plane. We can conclude using
    // the 2D orientation of the 3D triangle that is the top one along the z-axis
    // in the neighborhood of `min_slope_he`.
    Projection_traits_xy_3<GT> p_gt;
    typename Projection_traits_xy_3<GT>::Orientation_2 orientation_2 = p_gt.orientation_2_object();

    typename boost::property_traits<VPMap>::reference p1 = get(vpmap, source(min_slope_he, pmesh));
    typename boost::property_traits<VPMap>::reference p2 = get(vpmap, target(min_slope_he, pmesh));
    typename boost::property_traits<VPMap>::reference p3 = get(vpmap, target(next(min_slope_he, pmesh), pmesh));
    typename boost::property_traits<VPMap>::reference p4 = get(vpmap, target(next(opposite(min_slope_he, pmesh), pmesh), pmesh));

    Orientation p1p2p3_2d = orientation_2(p1, p2, p3);
    Orientation p2p1p4_2d = orientation_2(p2, p1, p4);

    CGAL_assertion( p1p2p3_2d!=COLLINEAR || p2p1p4_2d!=COLLINEAR ); // no self-intersection

    if ( p1p2p3_2d == COLLINEAR)
      return p2p1p4_2d == LEFT_TURN;
    if (p2p1p4_2d ==COLLINEAR)
      return p1p2p3_2d == LEFT_TURN;

    // if the local dihedral angle is strictly larger that PI/2, we can conclude with any of two triangles
    if (p1p2p3_2d==p2p1p4_2d)
      return p1p2p3_2d == LEFT_TURN;

    typename GT::Orientation_3 orientation_3 = gt.orientation_3_object();

    CGAL_assertion( orientation_3(p1, p2, p3, p4) != COPLANAR ); // same side of min_slope_he and no self-intersection

    // if p1p2p3_2d is left turn, then it must be the top face so that the orientation is outward oriented
    if (p1p2p3_2d == LEFT_TURN)
      return orientation_3(p1, p2, p3, p4) == NEGATIVE;

    // same test with the other face
    CGAL_assertion(p2p1p4_2d == LEFT_TURN);
    return orientation_3(p2, p1, p4, p3) == NEGATIVE;
  }
} // end of namespace internal

/**
 * \ingroup PMP_orientation_grp
 * tests whether a closed polygon mesh has a positive orientation.
 * A closed polygon mesh is considered to have a positive orientation if the normal vectors
 * to all its faces point outside the domain bounded by the polygon mesh.
 * The normal vector to each face is chosen pointing on the side of the face
 * where its sequence of vertices is seen counterclockwise.
 * @pre `CGAL::is_closed(pmesh)`
 * @pre If `pmesh` contains several connected components, they are oriented consistently.
 *      In other words, the answer to this predicate would be the same for each
 *      isolated connected component.
 *
 * @tparam PolygonMesh a model of `FaceListGraph`
 * @tparam NamedParameters a sequence of \ref pmp_namedparameters "Named Parameters"
 *
 * @param pmesh the closed polygon mesh to be tested
 * @param np optional sequence of \ref pmp_namedparameters "Named Parameters" among the ones listed below
 *
 * \cgalNamedParamsBegin
 *    \cgalParamBegin{vertex_point_map} the property map with the points associated to the vertices of `pmesh` \cgalParamEnd
 *    \cgalParamBegin{geom_traits} a geometric traits class instance \cgalParamEnd
 * \cgalNamedParamsEnd
 *
 * \todo code : The following only handles polyhedron with one connected component
 *       the code, the sample example and the plugin must be updated.
 *
 * \sa `CGAL::Polygon_mesh_processing::reverse_face_orientations()`
 */
template<typename PolygonMesh, typename NamedParameters>
bool is_outward_oriented(const PolygonMesh& pmesh,
                         const NamedParameters& np)
{
  CGAL_warning(CGAL::is_closed(pmesh));
  CGAL_precondition(CGAL::is_valid_polygon_mesh(pmesh));

  //check for empty pmesh
  CGAL_warning(faces(pmesh).first != faces(pmesh).second);
  if (faces(pmesh).first == faces(pmesh).second)
    return true;

  using boost::choose_param;
  using boost::get_param;

  //VertexPointMap
  typedef typename GetVertexPointMap<PolygonMesh, NamedParameters>::const_type VPMap;
  VPMap vpmap = choose_param(get_param(np, internal_np::vertex_point),
                             get_const_property_map(vertex_point, pmesh));
  //Kernel
  typedef typename GetGeomTraits<PolygonMesh, NamedParameters>::type GT;
  GT gt = choose_param(get_param(np, internal_np::geom_traits), GT());

  //find the vertex with maximal z coordinate
  internal::Compare_vertex_points_z_3<GT, VPMap> less_z(vpmap, gt);
  typename boost::graph_traits<PolygonMesh>::vertex_descriptor v_max = *(vertices(pmesh).first);
  for (typename boost::graph_traits<PolygonMesh>::vertex_iterator
          vit=cpp11::next(vertices(pmesh).first), vit_end = vertices(pmesh).second;
          vit!=vit_end; ++vit)
  {
    // skip isolated vertices
    if (halfedge(*vit, pmesh)==boost::graph_traits<PolygonMesh>::null_halfedge())
      continue;
    if( less_z(v_max, *vit) )
      v_max=*vit;
  }

  // only isolated vertices
  if (halfedge(v_max, pmesh)==boost::graph_traits<PolygonMesh>::null_halfedge())
    return true;

  return internal::is_outward_oriented(v_max, pmesh, np);
}

///\cond SKIP_IN_MANUAL

template<typename PolygonMesh>
bool is_outward_oriented(const PolygonMesh& pmesh)
{
  return is_outward_oriented(pmesh,
    CGAL::Polygon_mesh_processing::parameters::all_default());
}

/// \endcond

template<typename PolygonMesh>
void reverse_orientation(typename boost::graph_traits<PolygonMesh>::halfedge_descriptor first, PolygonMesh& pmesh)
{
  typedef typename boost::graph_traits<PolygonMesh>::halfedge_descriptor halfedge_descriptor;
  typedef typename boost::graph_traits<PolygonMesh>::vertex_descriptor vertex_descriptor;
    if ( first == halfedge_descriptor())
        return;
    halfedge_descriptor last  = first;
    halfedge_descriptor prev  = first;
    halfedge_descriptor start = first;
    first = next(first, pmesh);
    vertex_descriptor  new_v = target( start, pmesh);
    while (first != last) {
      vertex_descriptor  tmp_v = target( first, pmesh);
      set_target( first, new_v, pmesh);
      set_halfedge(new_v, first, pmesh);
        new_v = tmp_v;
        halfedge_descriptor n = next(first, pmesh);
        set_next(first, prev, pmesh);
        prev  = first;
        first = n;
    }
    set_target( start, new_v, pmesh);
    set_halfedge( new_v, start, pmesh);
    set_next(start, prev,pmesh);
}

/**
* \ingroup PMP_orientation_grp
* reverses for each face the order of the vertices along the face boundary.
*
* @tparam PolygonMesh a model of `FaceListGraph` and `MutableFaceGraph`
*/
template<typename PolygonMesh>
void reverse_face_orientations(PolygonMesh& pmesh)
{
  typedef typename boost::graph_traits<PolygonMesh>::face_descriptor face_descriptor;
  typedef typename boost::graph_traits<PolygonMesh>::halfedge_descriptor halfedge_descriptor;
  BOOST_FOREACH(face_descriptor fd, faces(pmesh)){
    reverse_orientation(halfedge(fd,pmesh),pmesh);
  }
  // Note: A border edge is now parallel to its opposite edge.
  // We scan all border edges for this property. If it holds, we
  // reorient the associated hole and search again until no border
  // edge with that property exists any longer. Then, all holes are
  // reoriented.
  BOOST_FOREACH(halfedge_descriptor h, halfedges(pmesh)){
    if ( is_border(h,pmesh) &&
         target(h,pmesh) == target(opposite(h,pmesh),pmesh)){
      reverse_orientation(h, pmesh);
    }
  }
}

// Do the same thing as `reverse_face_orientations()` except that for
// the reversal of the border cycles (last step in the aforementioned function),
// this function guarantees that each cycle is reversed only once. This is
// particularly useful if you mesh contains polylines (i.e. edge which halfedges
// are both border halfedges).
template<typename PolygonMesh>
void reverse_face_orientations_of_mesh_with_polylines(PolygonMesh& pmesh)
{
  typedef typename boost::graph_traits<PolygonMesh>::face_descriptor face_descriptor;
  typedef typename boost::graph_traits<PolygonMesh>::halfedge_descriptor halfedge_descriptor;

  // reverse the orientation of each face
  BOOST_FOREACH(face_descriptor fd, faces(pmesh))
    reverse_orientation(halfedge(fd,pmesh),pmesh);

  //extract all border cycles
  boost::unordered_set<halfedge_descriptor> already_seen;
  std::vector<halfedge_descriptor> border_cycles;
  BOOST_FOREACH(halfedge_descriptor h, halfedges(pmesh))
    if ( is_border(h,pmesh) && already_seen.insert(h).second )
    {
      border_cycles.push_back(h);
      BOOST_FOREACH(halfedge_descriptor h2, halfedges_around_face(h,pmesh))
        already_seen.insert(h2);
    }

  // now reverse the border cycles
  BOOST_FOREACH(halfedge_descriptor h, border_cycles)
    reverse_orientation(h, pmesh);
}

/**
* \ingroup PMP_orientation_grp
* reverses for each face in `face_range` the order of the vertices along the face boundary.
* The function does not perform any control and if the orientation change of the faces
* makes the polygon mesh invalid, the behavior is undefined.
*
* @tparam PolygonMesh a model of `FaceListGraph` and `MutableFaceGraph`
* @tparam FaceRange range of face descriptors, model of `Range`.
*         Its iterator type is `InputIterator`.
*/
template<typename PolygonMesh, typename FaceRange>
void reverse_face_orientations(const FaceRange& face_range, PolygonMesh& pmesh)
{
  typedef typename boost::graph_traits<PolygonMesh>::face_descriptor face_descriptor;
  typedef typename boost::graph_traits<PolygonMesh>::halfedge_descriptor halfedge_descriptor;
  BOOST_FOREACH(face_descriptor fd, face_range){
    reverse_orientation(halfedge(fd,pmesh),pmesh);
  }

  // Note: A border edge is now parallel to its opposite edge.
  // We scan all border edges for this property. If it holds, we
  // reorient the associated hole and search again until no border
  // edge with that property exists any longer. Then, all holes are
  // reoriented.
  BOOST_FOREACH(face_descriptor fd, face_range)
    BOOST_FOREACH(halfedge_descriptor hd,
                  halfedges_around_face(halfedge(fd, pmesh), pmesh))
    {
      halfedge_descriptor ohd = opposite(hd, pmesh);
      if ( is_border(ohd, pmesh) &&
         target(hd,pmesh) == target(ohd,pmesh))
      {
        reverse_orientation(ohd, pmesh);
      }
    }
}
namespace internal {

template <class Kernel, class TriangleMesh, class VD, class Fid_map, class Vpm>
bool recursive_does_bound_a_volume(const TriangleMesh& tm,
                                         Vpm& vpm,
                                         Fid_map& fid_map,
                                   const std::vector<VD>& xtrm_vertices,
                                         boost::dynamic_bitset<>& cc_handled,
                                   const std::vector<std::size_t>& face_cc,
                                         std::size_t xtrm_cc_id,
                                         bool is_parent_outward_oriented)
{
  typedef boost::graph_traits<TriangleMesh> GT;
  typedef typename GT::face_descriptor face_descriptor;
  typedef Side_of_triangle_mesh<TriangleMesh, Kernel, Vpm> Side_of_tm;
// first check that the orientation of the current cc is consistant with its
// parent cc containing it
  bool new_is_parent_outward_oriented = internal::is_outward_oriented(
         xtrm_vertices[xtrm_cc_id], tm, parameters::vertex_point_map(vpm));
  if (new_is_parent_outward_oriented==is_parent_outward_oriented)
    return false;
  cc_handled.set(xtrm_cc_id);

  std::size_t nb_cc = cc_handled.size();

// get all cc that are inside xtrm_cc_id
  std::vector<face_descriptor> cc_faces;
  BOOST_FOREACH(face_descriptor fd, faces(tm))
  {
    if(face_cc[get(fid_map, fd)]==xtrm_cc_id)
      cc_faces.push_back(fd);
  }

  typename Side_of_tm::AABB_tree aabb_tree(cc_faces.begin(), cc_faces.end(),
                                           tm, vpm);
  Side_of_tm side_of_cc(aabb_tree);

  std::vector<std::size_t> cc_inside;
  for(std::size_t id=0; id<nb_cc; ++id)
  {
    if (cc_handled.test(id)) continue;
    if (side_of_cc(get(vpm,xtrm_vertices[id]))==ON_BOUNDED_SIDE)
      cc_inside.push_back(id);
  }

// check whether we need another recursion for cc inside xtrm_cc_id
  if (!cc_inside.empty())
  {
    std::size_t new_xtrm_cc_id = cc_inside.front();
    boost::dynamic_bitset<> new_cc_handled(nb_cc,0);
    new_cc_handled.set();
    new_cc_handled.reset(new_xtrm_cc_id);
    cc_handled.set(new_xtrm_cc_id);

    std::size_t nb_candidates = cc_inside.size();
    for (std::size_t i=1;i<nb_candidates;++i)
    {
      std::size_t candidate = cc_inside[i];
      if(get(vpm,xtrm_vertices[candidate]).z() >
         get(vpm,xtrm_vertices[new_xtrm_cc_id]).z()) new_xtrm_cc_id=candidate;
      new_cc_handled.reset(candidate);
      cc_handled.set(candidate);
    }

    if ( !internal::recursive_does_bound_a_volume<Kernel>(
           tm, vpm, fid_map, xtrm_vertices, new_cc_handled, face_cc,
           new_xtrm_cc_id, new_is_parent_outward_oriented) ) return false;
  }

// now explore remaining cc included in the same cc as xtrm_cc_id
  boost::dynamic_bitset<> cc_not_handled = ~cc_handled;
  std::size_t new_xtrm_cc_id = cc_not_handled.find_first();
  if (new_xtrm_cc_id == cc_not_handled.npos) return true;

  for (std::size_t candidate = cc_not_handled.find_next(new_xtrm_cc_id);
                   candidate < cc_not_handled.npos;
                   candidate = cc_not_handled.find_next(candidate))
  {
     if(get(vpm,xtrm_vertices[candidate]).z() > get(vpm,xtrm_vertices[new_xtrm_cc_id]).z())
        new_xtrm_cc_id = candidate;
  }

  return internal::recursive_does_bound_a_volume<Kernel>(
            tm, vpm, fid_map, xtrm_vertices, cc_handled, face_cc,
            new_xtrm_cc_id, is_parent_outward_oriented);
}

template <class Kernel, class TriangleMesh, class VD, class Fid_map, class Vpm>
void recursive_orient_volume_ccs( TriangleMesh& tm,
                                  Vpm& vpm,
                                  Fid_map& fid_map,
                                  const std::vector<VD>& xtrm_vertices,
                                  boost::dynamic_bitset<>& cc_handled,
                                  const std::vector<std::size_t>& face_cc,
                                  std::size_t xtrm_cc_id,
                                  bool is_parent_outward_oriented)
{
  typedef boost::graph_traits<TriangleMesh> Graph_traits;
  typedef typename Graph_traits::face_descriptor face_descriptor;
  typedef Side_of_triangle_mesh<TriangleMesh, Kernel, Vpm> Side_of_tm;
  std::vector<face_descriptor> cc_faces;
  BOOST_FOREACH(face_descriptor fd, faces(tm))
  {
    if(face_cc[get(fid_map, fd)]==xtrm_cc_id)
      cc_faces.push_back(fd);
  }
// first check that the orientation of the current cc is consistant with its
// parent cc containing it
  bool new_is_parent_outward_oriented = internal::is_outward_oriented(
         xtrm_vertices[xtrm_cc_id], tm, parameters::vertex_point_map(vpm));
  if (new_is_parent_outward_oriented==is_parent_outward_oriented)
  {
    Polygon_mesh_processing::reverse_face_orientations(cc_faces, tm);
    new_is_parent_outward_oriented = !new_is_parent_outward_oriented;
  }
  cc_handled.set(xtrm_cc_id);

  std::size_t nb_cc = cc_handled.size();

// get all cc that are inside xtrm_cc_id

  typename Side_of_tm::AABB_tree aabb_tree(cc_faces.begin(), cc_faces.end(),
                                           tm, vpm);
  Side_of_tm side_of_cc(aabb_tree);

  std::vector<std::size_t> cc_inside;
  for(std::size_t id=0; id<nb_cc; ++id)
  {
    if (cc_handled.test(id)) continue;
    if (side_of_cc(get(vpm,xtrm_vertices[id]))==ON_BOUNDED_SIDE)
      cc_inside.push_back(id);
  }

// check whether we need another recursion for cc inside xtrm_cc_id
  if (!cc_inside.empty())
  {
    std::size_t new_xtrm_cc_id = cc_inside.front();
    boost::dynamic_bitset<> new_cc_handled(nb_cc,0);
    new_cc_handled.set();
    new_cc_handled.reset(new_xtrm_cc_id);
    cc_handled.set(new_xtrm_cc_id);

    std::size_t nb_candidates = cc_inside.size();
    for (std::size_t i=1;i<nb_candidates;++i)
    {
      std::size_t candidate = cc_inside[i];
      if(get(vpm,xtrm_vertices[candidate]).z() >
         get(vpm,xtrm_vertices[new_xtrm_cc_id]).z()) new_xtrm_cc_id=candidate;
      new_cc_handled.reset(candidate);
      cc_handled.set(candidate);
    }

    internal::recursive_orient_volume_ccs<Kernel>(
           tm, vpm, fid_map, xtrm_vertices, new_cc_handled, face_cc,
           new_xtrm_cc_id, new_is_parent_outward_oriented);
  }

// now explore remaining cc included in the same cc as xtrm_cc_id
  boost::dynamic_bitset<> cc_not_handled = ~cc_handled;
  std::size_t new_xtrm_cc_id = cc_not_handled.find_first();
  if (new_xtrm_cc_id == cc_not_handled.npos) return ;

  for (std::size_t candidate = cc_not_handled.find_next(new_xtrm_cc_id);
                   candidate < cc_not_handled.npos;
                   candidate = cc_not_handled.find_next(candidate))
  {
     if(get(vpm,xtrm_vertices[candidate]).z() > get(vpm,xtrm_vertices[new_xtrm_cc_id]).z())
        new_xtrm_cc_id = candidate;
  }

  internal::recursive_orient_volume_ccs<Kernel>(
            tm, vpm, fid_map, xtrm_vertices, cc_handled, face_cc,
            new_xtrm_cc_id, is_parent_outward_oriented);
}

}//end internal

/**
* \ingroup PMP_orientation_grp
* makes each connected component of a closed triangulated surface mesh
* inward or outward oriented.
*
* @tparam TriangleMesh a model of `FaceListGraph` and `MutableFaceGraph` .
*                      If `TriangleMesh` has an internal property map for `CGAL::face_index_t`,
*                      as a named parameter, then it must be initialized.
* @tparam NamedParameters a sequence of \ref pmp_namedparameters
*
* @param tm a closed triangulated surface mesh
* @param np optional sequence of \ref pmp_namedparameters among the ones listed below
*
* \cgalNamedParamsBegin
*   \cgalParamBegin{vertex_point_map}
*     the property map with the points associated to the vertices of `tm`.
*     If this parameter is omitted, an internal property map for
*     `CGAL::vertex_point_t` must be available in `TriangleMesh`
*   \cgalParamEnd
*   \cgalParamBegin{face_index_map}
*     a property map containing the index of each face of `tm`.
*   \cgalParamEnd
*   \cgalParamBegin{outward_orientation}
*     if set to `true` (default) indicates that each connected component will be outward oriented,
*     (inward oriented if `false`).
*   \cgalParamEnd
* \cgalNamedParamsEnd
*/
template<class TriangleMesh, class NamedParameters>
void orient(TriangleMesh& tm, const NamedParameters& np)
{
  typedef boost::graph_traits<TriangleMesh> Graph_traits;
  typedef typename Graph_traits::vertex_descriptor vertex_descriptor;
  typedef typename Graph_traits::face_descriptor face_descriptor;
  typedef typename Graph_traits::halfedge_descriptor halfedge_descriptor;
  typedef typename GetVertexPointMap<TriangleMesh,
      NamedParameters>::const_type Vpm;
  typedef typename GetFaceIndexMap<TriangleMesh,
      NamedParameters>::const_type Fid_map;

  CGAL_assertion(is_triangle_mesh(tm));
  CGAL_assertion(is_valid_polygon_mesh(tm));
  CGAL_assertion(is_closed(tm));

  using boost::choose_param;
  using boost::get_param;

  bool orient_outward = choose_param(
                          get_param(np, internal_np::outward_orientation),true);

  Vpm vpm = choose_param(get_param(np, internal_np::vertex_point),
                         get_const_property_map(boost::vertex_point, tm));

  Fid_map fid_map = choose_param(get_param(np, internal_np::face_index),
                                 get_const_property_map(boost::face_index, tm));

  std::vector<std::size_t> face_cc(num_faces(tm), std::size_t(-1));

  // set the connected component id of each face
  std::size_t nb_cc = connected_components(tm,
                                           bind_property_maps(fid_map,make_property_map(face_cc)),
                                           parameters::face_index_map(fid_map));

  // extract a vertex with max z coordinate for each connected component
  std::vector<vertex_descriptor> xtrm_vertices(nb_cc, Graph_traits::null_vertex());
  BOOST_FOREACH(vertex_descriptor vd, vertices(tm))
  {
    halfedge_descriptor test_hd = halfedge(vd, tm);
    if(test_hd == Graph_traits::null_halfedge())
      continue;
    face_descriptor test_face = face(halfedge(vd, tm), tm);
    if(test_face == Graph_traits::null_face())
      test_face = face(opposite(halfedge(vd, tm), tm), tm);
    CGAL_assertion(test_face != Graph_traits::null_face());
    std::size_t cc_id = face_cc[get(fid_map,test_face )];
    if (xtrm_vertices[cc_id]==Graph_traits::null_vertex())
      xtrm_vertices[cc_id]=vd;
    else
      if (get(vpm, vd).z()>get(vpm,xtrm_vertices[cc_id]).z())
        xtrm_vertices[cc_id]=vd;
  }
  std::vector<std::vector<face_descriptor> > ccs(nb_cc);
  BOOST_FOREACH(face_descriptor fd, faces(tm))
  {
    ccs[face_cc[get(fid_map,fd)]].push_back(fd);
  }

  //orient ccs outward
  for(std::size_t id=0; id<nb_cc; ++id)
  {
    if(internal::is_outward_oriented(xtrm_vertices[id], tm, np)
        != orient_outward)
    {
      reverse_face_orientations(ccs[id], tm);
    }
  }
}

template<class TriangleMesh>
void orient(TriangleMesh& tm)
{
  orient(tm, parameters::all_default());
}

/** \ingroup PMP_orientation_grp
 *
 * indicates if `tm` bounds a volume.
 * See \ref coref_def_subsec for details.
 *
 * @tparam TriangleMesh a model of `MutableFaceGraph`, `HalfedgeListGraph` and `FaceListGraph`.
 * @tparam NamedParameters a sequence of \ref pmp_namedparameters "Named Parameters"
 *
 * @param tm a closed triangulated surface mesh
 * @param np optional sequence of \ref pmp_namedparameters "Named Parameters" among the ones listed below
 *
 * @pre `CGAL::is_closed(tm)`
 *
 * \cgalNamedParamsBegin
 *   \cgalParamBegin{vertex_point_map}
 *     the property map with the points associated to the vertices of `tm`.
 *     If this parameter is omitted, an internal property map for
 *     `CGAL::vertex_point_t` must be available in `TriangleMesh`
 *   \cgalParamEnd
 *   \cgalParamBegin{face_index_map}
 *     a property map containing the index of each face of `tm`.
 *   \cgalParamEnd
 * \cgalNamedParamsEnd
 *
 * \see `CGAL::Polygon_mesh_processing::orient_to_bound_a_volume()`
 */
template <class TriangleMesh, class NamedParameters>
bool does_bound_a_volume(const TriangleMesh& tm, const NamedParameters& np)
{
  typedef boost::graph_traits<TriangleMesh> GT;
  typedef typename GT::vertex_descriptor vertex_descriptor;
  typedef typename GetVertexPointMap<TriangleMesh,
                                     NamedParameters>::const_type Vpm;
  typedef typename GetFaceIndexMap<TriangleMesh,
                                   NamedParameters>::const_type Fid_map;
  typedef typename Kernel_traits<
    typename boost::property_traits<Vpm>::value_type >::Kernel Kernel;

  if (!is_closed(tm)) return false;
  if (!is_triangle_mesh(tm)) return false;

  Vpm vpm = boost::choose_param(boost::get_param(np, internal_np::vertex_point),
                                get_const_property_map(boost::vertex_point, tm));

  Fid_map fid_map = boost::choose_param(boost::get_param(np, internal_np::face_index),
                                        get_const_property_map(boost::face_index, tm));

  std::vector<std::size_t> face_cc(num_faces(tm), std::size_t(-1));

  // set the connected component id of each face
  std::size_t nb_cc = connected_components(tm,
                                bind_property_maps(fid_map,make_property_map(face_cc)),
                                parameters::face_index_map(fid_map));

  if (nb_cc == 1)
    return true;

  boost::dynamic_bitset<> cc_handled(nb_cc, 0);

  // extract a vertex with max z coordinate for each connected component
  std::vector<vertex_descriptor> xtrm_vertices(nb_cc, GT::null_vertex());
  BOOST_FOREACH(vertex_descriptor vd, vertices(tm))
  {
    std::size_t cc_id = face_cc[get(fid_map, face(halfedge(vd, tm), tm))];
    if (xtrm_vertices[cc_id]==GT::null_vertex())
      xtrm_vertices[cc_id]=vd;
    else
      if (get(vpm, vd).z()>get(vpm,xtrm_vertices[cc_id]).z())
        xtrm_vertices[cc_id]=vd;
  }

  //extract a vertex with max z amongst all components
  std::size_t xtrm_cc_id=0;
  for(std::size_t id=1; id<nb_cc; ++id)
    if (get(vpm, xtrm_vertices[id]).z()>get(vpm,xtrm_vertices[xtrm_cc_id]).z())
      xtrm_cc_id=id;

  bool is_parent_outward_oriented =
    !internal::is_outward_oriented(xtrm_vertices[xtrm_cc_id], tm, np);

  return internal::recursive_does_bound_a_volume<Kernel>(tm, vpm, fid_map,
                                                         xtrm_vertices,
                                                         cc_handled,
                                                         face_cc,
                                                         xtrm_cc_id,
                                                         is_parent_outward_oriented);
}

// doc: non-closed connected components are reported as isolated volumes
// doc: connected components with at least one non-triangle face are reported as isolated volumes
// doc: self-intersecting connect components are reported as isolated volumes.
// doc: CC intersecting another CC is reported as a seperate volume and so are its nested CCs
// doc: add option ignore_orientation_of_cc to control whether the inward/outward orientation of
//      component must be taken into account rather than only the nesting. In case of incompatible
//      orientation of a cc X with its parent, all other CC included in X (as well as X) are reported
//      as independant volumes
// TODO return a vector with info on the volume? non-triangle/open/SI/regular/...
template <class TriangleMesh, class FaceIndexMap, class NamedParameters>
std::size_t
volume_connected_components(const TriangleMesh& tm,
                                  FaceIndexMap volume_id_map,
                            const NamedParameters& np)
{
  typedef boost::graph_traits<TriangleMesh> GT;
  typedef typename GT::vertex_descriptor vertex_descriptor;
  typedef typename GT::face_descriptor face_descriptor;
  typedef typename GT::halfedge_descriptor halfedge_descriptor;
  typedef typename GetVertexPointMap<TriangleMesh,
                                     NamedParameters>::const_type Vpm;
  typedef typename GetFaceIndexMap<TriangleMesh,
                                   NamedParameters>::const_type Fid_map;
  typedef typename Kernel_traits<
    typename boost::property_traits<Vpm>::value_type >::Kernel Kernel;

  Vpm vpm = boost::choose_param(boost::get_param(np, internal_np::vertex_point),
                                get_const_property_map(boost::vertex_point, tm));

  // TODO handle user provided CC map ?
  Fid_map fid_map = boost::choose_param(boost::get_param(np, internal_np::face_index),
                                        get_const_property_map(boost::face_index, tm));

  std::vector<std::size_t> face_cc(num_faces(tm), std::size_t(-1));

// set the connected component id of each face
  const std::size_t nb_cc = connected_components(tm,
                              bind_property_maps(fid_map,make_property_map(face_cc)),
                              parameters::face_index_map(fid_map));

  if (nb_cc == 1)
  {
    BOOST_FOREACH(face_descriptor fd, faces(tm))
      put(volume_id_map, fd, 0);
    return 1;
  }

  boost::dynamic_bitset<> cc_handled(nb_cc, 0);
  std::vector<std::size_t> cc_volume_ids(nb_cc, -1);

  std::size_t next_volume_id = 0;
// First handle non-pure triangle meshes
  BOOST_FOREACH(face_descriptor fd, faces(tm))
  {
    if (!is_triangle(halfedge(fd, tm), tm))
    {
      std::size_t cc_id = face_cc[ get(fid_map, fd) ];
      if ( !cc_handled.test(cc_id) )
      {
        cc_handled.set(cc_id);
        cc_volume_ids[cc_id]=next_volume_id++;
      }
    }
  }

// Handle open connected components
  BOOST_FOREACH(halfedge_descriptor h, halfedges(tm))
  {
    if (is_border(h, tm))
    {
      face_descriptor fd = face( opposite(h, tm), tm );
      std::size_t cc_id = face_cc[ get(fid_map, fd) ];
      if ( !cc_handled.test(cc_id) )
      {
        cc_handled.set(cc_id);
        cc_volume_ids[cc_id]=next_volume_id++;
      }
    }
  }

// Handle self-intersecting connected components
  // TODO add an option for self-intersection handling (optional if known to be free from self-intersections
  typedef std::pair<face_descriptor, face_descriptor> Face_pair;
  std::vector<Face_pair> si_faces;
  std::set< std::pair<std::size_t, std::size_t> > self_intersecting_cc; // due to self-intersections
  self_intersections(tm, std::back_inserter(si_faces));
  std::vector<bool> is_involved_in_self_intersection(nb_cc, false);
  BOOST_FOREACH(const Face_pair& fp, si_faces)
  {
    std::size_t first_cc_id = face_cc[ get(fid_map, fp.first) ];
    std::size_t second_cc_id = face_cc[ get(fid_map, fp.second) ];

    if (first_cc_id==second_cc_id)
    {
      if ( !cc_handled.test(first_cc_id) )
      {
        cc_handled.set(first_cc_id);
        cc_volume_ids[first_cc_id]=next_volume_id++;
      }
    }
    else
    {
      is_involved_in_self_intersection[first_cc_id] = true;
      is_involved_in_self_intersection[second_cc_id] = true;
      self_intersecting_cc.insert( make_sorted_pair(first_cc_id, second_cc_id) );
    }
  }

  if (!cc_handled.all())
  {
  // extract a vertex with max z coordinate for each connected component
    std::vector<vertex_descriptor> xtrm_vertices(nb_cc, GT::null_vertex());
    BOOST_FOREACH(vertex_descriptor vd, vertices(tm))
    {
      std::size_t cc_id = face_cc[get(fid_map, face(halfedge(vd, tm), tm))];
      if (cc_handled.test(cc_id)) continue;
      if (xtrm_vertices[cc_id]==GT::null_vertex())
        xtrm_vertices[cc_id]=vd;
      else
        if (get(vpm, vd).z()>get(vpm,xtrm_vertices[cc_id]).z())
          xtrm_vertices[cc_id]=vd;
    }

    const bool ignore_orientation_of_cc = false; // TODO add an option

  // fill orientation vector for each surface CC
    boost::dynamic_bitset<> is_cc_outward_oriented;
    if (!ignore_orientation_of_cc)
    {
      is_cc_outward_oriented.resize(nb_cc);
      for(std::size_t cc_id=0; cc_id<nb_cc; ++cc_id)
      {
        if (cc_handled.test(cc_id)) continue;
        is_cc_outward_oriented[cc_id] = internal::is_outward_oriented(xtrm_vertices[cc_id], tm, np);
      }
    }

  //collect faces per CC
    std::vector< std::vector<face_descriptor> > faces_per_cc(nb_cc);
    std::vector< std::size_t > nb_faces_per_cc(nb_cc, 0);
    BOOST_FOREACH(face_descriptor fd, faces(tm))
    {
      std::size_t cc_id = face_cc[ get(fid_map, fd) ];
      ++nb_faces_per_cc[ cc_id ];
    }
    for (std::size_t i=0; i<nb_cc; ++i)
      faces_per_cc[i].reserve( nb_faces_per_cc[i] );
    BOOST_FOREACH(face_descriptor fd, faces(tm))
    {
      std::size_t cc_id = face_cc[ get(fid_map, fd) ];
      faces_per_cc[ cc_id ].push_back(fd);
    }

  // init the main loop
    std::size_t k = 0;
    // contains for each CC the CC that are in its bounded side
    std::vector<std::vector<std::size_t> > nested_cc_per_cc(nb_cc);
    // similar as above but exclusively contains cc ids included by more that one CC.
    // The result will be then merged with nested_cc_per_cc but temporarilly we need
    // another container to not more than once the inclusion testing (in case a CC is
    // included by more than 2 CC) + associate such CC to only one volume
    std::vector<std::vector<std::size_t> > nested_cc_per_cc_shared(nb_cc);
    std::vector < std::size_t > nesting_levels(nb_cc, 0); // indicates for each CC its nesting level
    std::vector < boost::dynamic_bitset<> > level_k_nestings; // container containing CCs in the same volume (one bitset per volume) at level k
    level_k_nestings.push_back( ~cc_handled );

  // the following loop is exploring the nesting level by level (0 -> max_level)
    while (!level_k_nestings.empty())
    {
      std::vector < boost::dynamic_bitset<> > level_k_plus_1_nestings;
      BOOST_FOREACH(boost::dynamic_bitset<> cc_to_handle, level_k_nestings)
      {
        CGAL_assertion( cc_to_handle.any() );
        while(cc_to_handle.any())
        {
        //extract a vertex with max z amongst all components
          std::size_t xtrm_cc_id=cc_to_handle.find_first();
          for(std::size_t id  = cc_to_handle.find_next(xtrm_cc_id);
                          id != cc_to_handle.npos;
                          id  = cc_to_handle.find_next(id))
          {
            if (get(vpm, xtrm_vertices[id]).z()>get(vpm,xtrm_vertices[xtrm_cc_id]).z())
              xtrm_cc_id=id;
          }
          cc_to_handle.reset(xtrm_cc_id);
          nesting_levels[xtrm_cc_id] = k;

        // collect id inside xtrm_cc_id CC
          typedef Side_of_triangle_mesh<TriangleMesh, Kernel, Vpm> Side_of_tm;
          typename Side_of_tm::AABB_tree aabb_tree(faces_per_cc[xtrm_cc_id].begin(),
                                                   faces_per_cc[xtrm_cc_id].end(),
                                                   tm, vpm);
          Side_of_tm side_of_cc(aabb_tree);

          std::vector<std::size_t> cc_intersecting; // contains id of CC intersecting xtrm_cc_id

          boost::dynamic_bitset<> nested_cc_to_handle(nb_cc, 0);
          for(std::size_t id  = cc_to_handle.find_first();
                          id != cc_to_handle.npos;
                          id  = cc_to_handle.find_next(id))
          {
            if (self_intersecting_cc.count( make_sorted_pair(xtrm_cc_id, id) )!= 0)
            {
              cc_intersecting.push_back(id);
              nesting_levels[id] = k; // same level as xtrm_cc_id
              continue; // to not dot inclusion test for intersecting CCs
            }

            if (side_of_cc(get(vpm,xtrm_vertices[id]))==ON_BOUNDED_SIDE)
            {
              nested_cc_per_cc[xtrm_cc_id].push_back(id);
              // mark nested CC as handle and collect them for the handling of the next level
              nested_cc_to_handle.set(id);
              cc_to_handle.reset(id);
            }
          }

        //for each CC intersecting xtrm_cc_id, find the CCs included in both
          BOOST_FOREACH(std::size_t id, cc_intersecting)
          {
            typename Side_of_tm::AABB_tree aabb_tree(faces_per_cc[id].begin(),
                                                     faces_per_cc[id].end(),
                                                     tm, vpm);
            Side_of_tm side_of_cc(aabb_tree);
            BOOST_FOREACH(std::size_t ncc_id, nested_cc_per_cc[xtrm_cc_id])
            {
              if (self_intersecting_cc.count( make_sorted_pair(ncc_id, id) )!= 0)
                continue;
              if (side_of_cc(get(vpm,xtrm_vertices[ncc_id]))==ON_BOUNDED_SIDE)
                nested_cc_per_cc_shared[id].push_back(ncc_id);
            }
          }

          if ( nested_cc_per_cc[xtrm_cc_id].empty() ) continue;
          level_k_plus_1_nestings.push_back(nested_cc_to_handle);
        }
      }
      ++k;
      level_k_nestings.swap(level_k_plus_1_nestings);
    }
  // detect inconsistencies of the orientation at the level 0
  // and check if all CC at level 0 are in the same volume
    std::size_t ref_cc_id = nb_cc;
    std::size_t FIRST_LEVEL = 0; // used to know if even or odd nesting is the top level
    if(!ignore_orientation_of_cc)
    {
      for(std::size_t cc_id=0; cc_id<nb_cc; ++cc_id)
      {
        if (cc_handled.test(cc_id)) continue;
        if( nesting_levels[cc_id]==0 )
        {
          if(ref_cc_id==nb_cc)
            ref_cc_id=cc_id;
          else
            if( is_cc_outward_oriented[cc_id] != is_cc_outward_oriented[ref_cc_id] )
            {
              // all is indefinite
              for(std::size_t id=0; id<nb_cc; ++id)
              {
                if (cc_handled.test(cc_id)) continue;
                cc_volume_ids[id] = next_volume_id++;
              }
              cc_handled.set();
              break;
            }
        }
      }

      if (!cc_handled.all() && !is_cc_outward_oriented[ref_cc_id])
      {
        // all level 0 CC are in the same volume
        for(std::size_t cc_id=0; cc_id<nb_cc; ++cc_id)
        {
          if (cc_handled.test(cc_id)) continue;
          if( nesting_levels[cc_id]==0 )
          {
            cc_handled.set(cc_id);
            cc_volume_ids[cc_id]=next_volume_id;
          }
        }
        ++next_volume_id;
        FIRST_LEVEL = 1;
      }
    }

  // apply volume classification using level 0 nesting
    for(std::size_t cc_id=0; (!cc_handled.all()) && cc_id<nb_cc; ++cc_id)
    {
      if (cc_handled.test(cc_id)) continue;
      // TODO handle orientation of level 0 that will also change the way
      // volume are build
      CGAL_assertion( nesting_levels[cc_id]!=0 || is_cc_outward_oriented[cc_id] );
      if( nesting_levels[cc_id]%2 != FIRST_LEVEL ) continue;
      cc_handled.set(cc_id);
      cc_volume_ids[cc_id] = next_volume_id++;

      //if the CC is involved in a self-intersection all nested CC are put in a seperate volumes
      if (is_involved_in_self_intersection[cc_id])
      {
        BOOST_FOREACH(std::size_t ncc_id, nested_cc_per_cc[cc_id])
        {
          cc_handled.set(ncc_id);
          cc_volume_ids[ncc_id] = next_volume_id++;
        }
        continue;
      }

      BOOST_FOREACH(std::size_t ncc_id, nested_cc_per_cc[cc_id])
      {
        if ( nesting_levels[ncc_id]==nesting_levels[cc_id]+1 )
        {
          cc_handled.set(ncc_id);
          if (!ignore_orientation_of_cc)
          {
            if (is_cc_outward_oriented[cc_id]==is_cc_outward_oriented[ncc_id])
            {
              // the surface component has an incorrect orientation wrt to its parent:
              // we dump it and all included surface components as independant volumes.
              cc_volume_ids[ncc_id] = next_volume_id++;
              BOOST_FOREACH(std::size_t nncc_id, nested_cc_per_cc[ncc_id])
              {
                cc_handled.set(nncc_id);
                cc_volume_ids[nncc_id] = next_volume_id++;
              }
              continue;
            }
          }
          cc_volume_ids[ncc_id] = cc_volume_ids[cc_id];
        }
      }
    }

  // merge nested_cc_per_cc and nested_cc_per_cc_shared
  // (done after the volume creation to assign a CC to a unique volume)
    for(std::size_t id=0; id<nb_cc; ++id)
    {
      if (!nested_cc_per_cc_shared[id].empty())
        nested_cc_per_cc[id].insert(nested_cc_per_cc[id].end(),
                                    nested_cc_per_cc_shared[id].begin(),
                                    nested_cc_per_cc_shared[id].end());
    }

    // TODO: add nesting_parent as an optional output parameter
    //       note that this will require to also output the cc_id map
  // extract direct nested parent (more than one in case of self-intersection)
    std::vector< std::vector<std::size_t> > nesting_parent(nb_cc);
    for(std::size_t cc_id=0; cc_id<nb_cc; ++cc_id)
    {
      BOOST_FOREACH(std::size_t ncc_id, nested_cc_per_cc[cc_id])
      {
        if (nesting_levels[cc_id]+1 == nesting_levels[ncc_id])
          nesting_parent[ncc_id].push_back(cc_id);
      }
    }

  // update volume id map
    for(std::size_t cc_id=0; cc_id<nb_cc; ++cc_id)
    {
      BOOST_FOREACH(face_descriptor fd, faces_per_cc[cc_id])
      put(volume_id_map, fd, cc_volume_ids[cc_id]);
    }
  }
  else
  {
    BOOST_FOREACH(face_descriptor fd, faces(tm))
    {
      std::size_t cc_id = face_cc[ get(fid_map, fd) ];
      put(volume_id_map, fd, cc_volume_ids[cc_id]);
    }
  }

  return next_volume_id;
}

/// \cond SKIP_IN_MANUAL
template <class TriangleMesh>
bool does_bound_a_volume(const TriangleMesh& tm)
{
  return does_bound_a_volume(tm, parameters::all_default());
}

template <class TriangleMesh, class FaceIndexMap>
std::size_t volume_connected_components(const TriangleMesh& tm, FaceIndexMap volume_id_map)
{
  return volume_connected_components(tm, volume_id_map, parameters::all_default());
}
/// \endcond


/** \ingroup PMP_orientation_grp
 *
 * orients the connected components of `tm` to make it bound a volume.
 * See \ref coref_def_subsec for a precise definition.
 *
 * @tparam TriangleMesh a model of `MutableFaceGraph`, `HalfedgeListGraph` and `FaceListGraph`.
 *                      If `TriangleMesh` has an internal property map for `CGAL::face_index_t`,
 *                      as a named parameter, then it must be initialized.
 * @tparam NamedParameters a sequence of \ref pmp_namedparameters
 *
 * @param tm a closed triangulated surface mesh
 * @param np optional sequence of \ref pmp_namedparameters among the ones listed below
 *
 * \cgalNamedParamsBegin
 *   \cgalParamBegin{vertex_point_map}
 *     the property map with the points associated to the vertices of `tm`.
 *     If this parameter is omitted, an internal property map for
 *     `CGAL::vertex_point_t` must be available in `TriangleMesh`
 *   \cgalParamEnd
 *   \cgalParamBegin{face_index_map}
 *     a property map containing the index of each face of `tm`.
 *   \cgalParamEnd
 *   \cgalParamBegin{outward_orientation}
 *     if set to `true` (default) the outer connected components will be outward oriented (inward oriented if set to `false`).
 *     If the outer connected components are inward oriented, it means that the infinity will be considered
 *     as part of the volume bounded by `tm`.
 *   \cgalParamEnd
 * \cgalNamedParamsEnd
 *
 * \see `CGAL::Polygon_mesh_processing::does_bound_a_volume()`
 */
template <class TriangleMesh, class NamedParameters>
void orient_to_bound_a_volume(TriangleMesh& tm,
                                        const NamedParameters& np)
{
  typedef boost::graph_traits<TriangleMesh> Graph_traits;
  typedef typename Graph_traits::vertex_descriptor vertex_descriptor;
  typedef typename GetVertexPointMap<TriangleMesh,
      NamedParameters>::const_type Vpm;
  typedef typename GetFaceIndexMap<TriangleMesh,
      NamedParameters>::const_type Fid_map;
  typedef typename Kernel_traits<
      typename boost::property_traits<Vpm>::value_type >::Kernel Kernel;
  if (!is_closed(tm)) return;
  if (!is_triangle_mesh(tm)) return;

  using boost::choose_param;
  using boost::get_param;

  bool orient_outward = choose_param(
                          get_param(np, internal_np::outward_orientation),true);

  Vpm vpm = choose_param(get_param(np, internal_np::vertex_point),
                         get_const_property_map(boost::vertex_point, tm));

  Fid_map fid_map = choose_param(get_param(np, internal_np::face_index),
                                 get_const_property_map(boost::face_index, tm));

  std::vector<std::size_t> face_cc(num_faces(tm), std::size_t(-1));


  // set the connected component id of each face
  std::size_t nb_cc = connected_components(tm,
                                           bind_property_maps(fid_map,make_property_map(face_cc)),
                                           parameters::face_index_map(fid_map));

  if (nb_cc == 1)
  {
    if( orient_outward != is_outward_oriented(tm))
      reverse_face_orientations(faces(tm), tm);
    return ;
  }


  boost::dynamic_bitset<> cc_handled(nb_cc, 0);

  // extract a vertex with max z coordinate for each connected component
  std::vector<vertex_descriptor> xtrm_vertices(nb_cc, Graph_traits::null_vertex());
  BOOST_FOREACH(vertex_descriptor vd, vertices(tm))
  {
    std::size_t cc_id = face_cc[get(fid_map, face(halfedge(vd, tm), tm))];
    if (xtrm_vertices[cc_id]==Graph_traits::null_vertex())
      xtrm_vertices[cc_id]=vd;
    else
      if (get(vpm, vd).z()>get(vpm,xtrm_vertices[cc_id]).z())
        xtrm_vertices[cc_id]=vd;
  }

  //extract a vertex with max z amongst all components
  std::size_t xtrm_cc_id=0;
  for(std::size_t id=1; id<nb_cc; ++id)
    if (get(vpm, xtrm_vertices[id]).z()>get(vpm,xtrm_vertices[xtrm_cc_id]).z())
      xtrm_cc_id=id;

  bool is_parent_outward_oriented =
      ! orient_outward;

  internal::recursive_orient_volume_ccs<Kernel>(tm, vpm, fid_map,
                                                xtrm_vertices,
                                                cc_handled,
                                                face_cc,
                                                xtrm_cc_id,
                                                is_parent_outward_oriented);
}

template <class TriangleMesh>
void orient_to_bound_a_volume(TriangleMesh& tm)
{
  orient_to_bound_a_volume(tm, parameters::all_default());
}
} // namespace Polygon_mesh_processing
} // namespace CGAL
#endif // CGAL_ORIENT_POLYGON_MESH_H
