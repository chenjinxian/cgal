// Copyright (c) 2009 INRIA Sophia-Antipolis (France).
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
//
//
// Author(s)     : Stéphane Tayeb
//
//******************************************************************************
// File Description :
// Implements a mesher level for facets.
//******************************************************************************

#ifndef CGAL_MESH_3_REFINE_FACETS_3_H
#define CGAL_MESH_3_REFINE_FACETS_3_H

#include <CGAL/Mesher_level.h>
#include <CGAL/Mesher_level_default_implementations.h>
#ifdef CGAL_LINKED_WITH_TBB
  #include <tbb/tbb.h>
#endif

#include <CGAL/Meshes/Filtered_deque_container.h>
#include <CGAL/Meshes/Filtered_multimap_container.h>
#include <CGAL/Meshes/Double_map_container.h>

#include <CGAL/Meshes/Triangulation_mesher_level_traits_3.h>

#ifdef MESH_3_PROFILING
  #include <CGAL/Mesh_3/Profiling_tools.h>
#endif

#include <boost/format.hpp>
#include <boost/optional.hpp>
#include <boost/mpl/has_xxx.hpp>
#include <boost/mpl/if.hpp>
#include <CGAL/tuple.h>
#include <sstream>
#include "boost/tuple/tuple.hpp"

namespace CGAL {

namespace Mesh_3 {

// Helper meta-programming functions, to allow backward compatibility.
//
//   - Has_Is_facet_bad and Had_Facet_badness are used to detect if a model
//     of the MeshFacetCriteria_3 concept follows the specifications of
//     CGAL-3.7 (with Facet_badness) or later (with Is_facet_bad).
//
//   - Then the meta-function Get_Is_facet_bad is used to get the actual
//     type, either Facet_criteria::Facet_badness or
//      Facet_criteria::Is_facet_bad.

BOOST_MPL_HAS_XXX_TRAIT_NAMED_DEF(Has_Is_facet_bad, Is_facet_bad, true)
BOOST_MPL_HAS_XXX_TRAIT_NAMED_DEF(Has_Facet_badness, Facet_badness, false)

//====== CJTODO TEMP
template <typename Facet>
class ExplicitFacet
{
public:
  ExplicitFacet(const Facet &f)
  {
    for (int i = 0, j = 0; i < 4; ++i)
    {
      if (i != f.second)
      {
        points.insert(f.first->vertex(i)->point().x());
        points.insert(f.first->vertex(i)->point().y());
        points.insert(f.first->vertex(i)->point().z());
      }
    }
  }

  std::multiset<double> points;
  
  bool operator<(const ExplicitFacet& other) const
  {
    if (points.size() != other.points.size())
    {
      std::cerr << "************************ ERROR: points.size() != other.points.size() ************" << std::endl;
      return true;
    }

    for (std::multiset<double>::const_iterator it = points.begin(), it_other = other.points.begin() ;
      it != points.end() ; ++it)
    {
      if (*it > *it_other)
        return false;
      if (*it < *it_other)
        return true;
    }
    return false;
  }
};
#include <set>
//====== /CJTODO TEMP


// template class, used when use_facet_badness = false
template <typename Facet_criteria,
          bool use_facet_badness = (!Has_Is_facet_bad<Facet_criteria>::value) &&
                                    Has_Facet_badness<Facet_criteria>::value >
struct Get_Is_facet_bad {
  typedef typename Facet_criteria::Is_facet_bad Type;
  typedef Type type; // compatibility with Boost
};

// partial specialization when use_facet_badness == true
template <typename Facet_criteria>
struct Get_Is_facet_bad<Facet_criteria, true> {
  typedef typename Facet_criteria::Facet_badness Type;
  typedef Type type;
};

  // Predicate to know if a facet in a refinement queue is a zombie
  // A facet is a pair <cell, index of the opposite vertex>.
  // A facet is a "zombie" if at least one of its two adjacent cells
  // has been erased. We test it thanks to the "erase counter" which
  // is inside each cell (incremented by the compact container).
  // In the refinement queue, we store a tuple
  // <facet1, facet1_erase counter, facet2, facet2_erase_counter>
  // where facet2 = mirror_facet(facet1) and facetx_erase_counter is
  // the erase_counter of facetx's cell when added to the queue>
  template<typename Facet>
  class Facet_to_refine_is_not_zombie
  {
  public:
    Facet_to_refine_is_not_zombie() {}

    bool operator()(const boost::tuple<
      Facet, unsigned int, Facet, unsigned int> &f) const
    {
#ifdef _DEBUG
      int f1_current_erase_counter = boost::get<0>(f).first->get_erase_counter();
      int f1_saved_erase_counter = boost::get<1>(f);
      int f2_current_erase_counter = boost::get<2>(f).first->get_erase_counter();
      int f2_saved_erase_counter = boost::get<3>(f);
      //f1_current_erase_counter - f1_saved_erase_counter + f2_current_erase_counter - f2_saved_erase_counter == 1

      /*if (f1_current_erase_counter - f1_saved_erase_counter + f2_current_erase_counter - f2_saved_erase_counter == 1)
      {
#ifdef LINKED_WITH_TBB
        tbb::queuing_mutex mut;
        tbb::queuing_mutex::scoped_lock l(mut);
#endif

        std::stringstream sstr;
        Facet facet = boost::get<0>(f);
        sstr << "Facet 1 { " << std::endl
        << "  - " << *facet.first->vertex((facet.second+1)%4)  << std::endl
        << "  - " << *facet.first->vertex((facet.second+2)%4)  << std::endl
        << "  - " << *facet.first->vertex((facet.second+3)%4)  << std::endl
        << "  - 4th vertex in cell: " << *facet.first->vertex(facet.second)  << std::endl
        << "}" << std::endl;

        facet = boost::get<2>(f);
        sstr << "Facet 2 { " << std::endl
        << "  - " << *facet.first->vertex((facet.second+1)%4)  << std::endl
        << "  - " << *facet.first->vertex((facet.second+2)%4)  << std::endl
        << "  - " << *facet.first->vertex((facet.second+3)%4)  << std::endl
        << "  - 4th vertex in cell: " << *facet.first->vertex(facet.second)  << std::endl
        << "}" << std::endl;

        std::string s = sstr.str();
        //std::cerr << s << std::endl;
      }*/
#endif
      return (boost::get<0>(f).first->get_erase_counter() == boost::get<1>(f)
        && boost::get<2>(f).first->get_erase_counter() == boost::get<3>(f) );
    }
  };

/************************************************
// Class Refine_facets_3_base
// Two versions: sequential / parallel
************************************************/

// Sequential
template <typename Index, typename Facet, typename Concurrency_tag>
class Refine_facets_3_base
{
protected:
  Refine_facets_3_base() : m_last_vertex_index() {}

  Index get_last_vertex_index() const
  {
    return m_last_vertex_index;
  }

  void set_last_vertex_index(Index i) const
  {
    m_last_vertex_index = i;
  }

#if defined(CGAL_MESH_3_USE_LAZY_SORTED_REFINEMENT_QUEUE) \
 || defined(CGAL_MESH_3_USE_LAZY_UNSORTED_REFINEMENT_QUEUE)

  boost::tuple<Facet, unsigned int, Facet, unsigned int>
  from_facet_to_refinement_queue_element(const Facet &facet,
                                         const Facet &mirror) const
  {
    return boost::make_tuple(
      facet, facet.first->get_erase_counter(),
      mirror, mirror.first->get_erase_counter());
  }

public:
  template<typename Container_element>
  Facet extract_element_from_container_value(const Container_element &e) const
  {
    // We get the first Facet inside the tuple
    return e.get<0>();
  }

#else

  Facet
  from_facet_to_refinement_queue_element(const Facet &facet,
                                         const Facet &mirror) const
  {
    // Returns canonical facet
    return (facet < mirror) ? facet : mirror;
  }

public:
  template<typename Container_element>
  Facet extract_element_from_container_value(const Container_element &e) const
  {
    return e;
  }

#endif

protected:
  /// Stores index of vertex that may be inserted into triangulation
  mutable Index m_last_vertex_index;
};

#ifdef CGAL_LINKED_WITH_TBB
// Parallel
template <typename Index, typename Facet>
class Refine_facets_3_base<Index, Facet, Parallel_tag>
{
protected:
  Refine_facets_3_base() : m_last_vertex_index(Index()) {}

  Index get_last_vertex_index() const
  {
    return m_last_vertex_index.local();
  }

  void set_last_vertex_index(Index i) const
  {
    m_last_vertex_index.local() = i;
  }

  boost::tuple<Facet, unsigned int, Facet, unsigned int>
  from_facet_to_refinement_queue_element(const Facet &facet,
                                         const Facet &mirror) const
  {
    return boost::make_tuple(
      facet, facet.first->get_erase_counter(),
      mirror, mirror.first->get_erase_counter());
  }

public:
  template<typename Container_element>
  Facet extract_element_from_container_value(const Container_element &e) const
  {
    // We get the first Facet inside the tuple
    return e.get<0>();
  }

protected:
  /// Stores index of vertex that may be inserted into triangulation
  mutable tbb::enumerable_thread_specific<Index> m_last_vertex_index;
};
#endif // CGAL_LINKED_WITH_TBB

/************************************************
// Class Refine_facets_3
//
// Template parameters should be models of
// Tr         : MeshTriangulation_3
// Criteria   : SurfaceMeshFacetsCriteria_3
// MeshDomain : MeshTraits_3
//
// Implements a Mesher_level for facets
************************************************/

// TODO document Container_ requirements
template<class Tr,
         class Criteria,
         class MeshDomain,
         class Complex3InTriangulation3,
         class Previous_level_,
         class Concurrency_tag,
#ifdef CGAL_LINKED_WITH_TBB
         class Container_ = typename boost::mpl::if_c // (parallel/sequential?)
         <
          boost::is_base_of<Parallel_tag, Concurrency_tag>::value,
          // Parallel
# ifdef CGAL_MESH_3_USE_LAZY_UNSORTED_REFINEMENT_QUEUE
          Meshes::Filtered_deque_container
# else
          Meshes::Filtered_multimap_container
# endif
          <
            boost::tuple<typename Tr::Facet, unsigned int,
                          typename Tr::Facet, unsigned int>,
            typename Criteria::Facet_quality,
            Facet_to_refine_is_not_zombie<typename Tr::Facet>,
            Concurrency_tag
          >,
          // Sequential
# ifdef CGAL_MESH_3_USE_LAZY_UNSORTED_REFINEMENT_QUEUE
          Meshes::Filtered_deque_container
          <
            boost::tuple<typename Tr::Facet, unsigned int,
                          typename Tr::Facet, unsigned int>,
            typename Criteria::Facet_quality,
            Facet_to_refine_is_not_zombie<typename Tr::Facet>,
            Concurrency_tag
          >
# elif defined(CGAL_MESH_3_USE_LAZY_SORTED_REFINEMENT_QUEUE)
          Meshes::Filtered_multimap_container
          <
            boost::tuple<typename Tr::Facet, unsigned int,
                          typename Tr::Facet, unsigned int>,
            typename Criteria::Facet_quality,
            Facet_to_refine_is_not_zombie<typename Tr::Facet>,
            Concurrency_tag
          >
# else
          Meshes::Double_map_container<typename Tr::Facet,
                                       typename Criteria::Facet_quality>
# endif
         >::type // boost::if (parallel/sequential)

#else // !CGAL_LINKED_WITH_TBB

         // Sequential
         class Container_ =
# ifdef CGAL_MESH_3_USE_LAZY_UNSORTED_REFINEMENT_QUEUE
          Meshes::Filtered_deque_container
          <
            boost::tuple<typename Tr::Facet, unsigned int,
                          typename Tr::Facet, unsigned int>,
            typename Criteria::Facet_quality,
            Facet_to_refine_is_not_zombie<typename Tr::Facet>,
            Concurrency_tag
          >
# elif defined(CGAL_MESH_3_USE_LAZY_SORTED_REFINEMENT_QUEUE)
          Meshes::Filtered_multimap_container
          <
            boost::tuple<typename Tr::Facet, unsigned int,
                          typename Tr::Facet, unsigned int>,
            typename Criteria::Facet_quality,
            Facet_to_refine_is_not_zombie<typename Tr::Facet>,
            Concurrency_tag
          >
# else
          Meshes::Double_map_container<typename Tr::Facet,
                                       typename Criteria::Facet_quality>
# endif
#endif // CGAL_LINKED_WITH_TBB
>
class Refine_facets_3
: public Refine_facets_3_base<typename MeshDomain::Index, typename Tr::Facet,
                              Concurrency_tag>
, public Mesher_level<Tr,
                      Refine_facets_3<Tr,
                                      Criteria,
                                      MeshDomain,
                                      Complex3InTriangulation3,
                                      Previous_level_,
                                      Concurrency_tag,
                                      Container_>,
                      typename Tr::Facet,
                      Previous_level_,
                      Triangulation_mesher_level_traits_3<Tr>,
                      Concurrency_tag >
, public Container_
, public No_after_no_insertion
, public No_before_conflicts
{
  // Self
  typedef Refine_facets_3<Tr,
                          Criteria,
                          MeshDomain,
                          Complex3InTriangulation3,
                          Previous_level_,
                          Concurrency_tag,
                          Container_>                  Self;

  typedef Refine_facets_3_base<typename MeshDomain::Index,
                               typename Tr::Facet,
                               Concurrency_tag>        Base;

  typedef Mesher_level<Tr,
                      Refine_facets_3<Tr,
                                      Criteria,
                                      MeshDomain,
                                      Complex3InTriangulation3,
                                      Previous_level_,
                                      Concurrency_tag,
                                      Container_>,
                      typename Tr::Facet,
                      Previous_level_,
                      Triangulation_mesher_level_traits_3<Tr>,
                      Concurrency_tag >               Base_ML;

public:
  using Base_ML::add_to_TLS_lists;
  using Base_ML::splice_local_lists;

  typedef Container_ Container; // Because we need it in Mesher_level
  typedef typename Container::Element Container_element;
  typedef typename Tr::Point Point;
  typedef typename Tr::Facet Facet;
  typedef typename Tr::Vertex_handle Vertex_handle;
  typedef typename Triangulation_mesher_level_traits_3<Tr>::Zone Zone;
  typedef Complex3InTriangulation3 C3T3;

  /// Constructor
  // For sequential
  Refine_facets_3(Tr& triangulation,
                  const Criteria& criteria,
                  const MeshDomain& oracle,
                  Previous_level_& previous,
                  C3T3& c3t3);
  // For parallel
  Refine_facets_3(Tr& triangulation,
                  const Criteria& criteria,
                  const MeshDomain& oracle,
                  Previous_level_& previous,
                  C3T3& c3t3,
                  Mesh_3::LockDataStructureType *p_lock_ds,
                  Mesh_3::WorksharingDataStructureType *p_worksharing_ds);

  /// Destructor
  virtual ~Refine_facets_3() { }

  /// Get a reference on triangulation
  Tr& triangulation_ref_impl() { return r_tr_; }
  const Tr& triangulation_ref_impl() const { return r_tr_; }

  /// Initialization function
  void scan_triangulation_impl();

  int get_number_of_bad_elements_impl();

  Point circumcenter_impl(const Facet& facet) const
  {
    return get_facet_surface_center(facet);
  }

  template <typename Mesh_visitor>
  void before_next_element_refinement_in_superior_impl(Mesh_visitor visitor)
  {
    // Before refining any cell, we refine the facets in the local refinement
    // queue
    this->treat_local_refinement_queue(visitor);
  }

  void before_next_element_refinement_impl()
  {
  }

  Facet get_next_element_impl()
  {
    return extract_element_from_container_value(
      Container_::get_next_element_impl());
  }

  Facet get_next_local_element_impl()
  {
    return extract_element_from_container_value(
      Container_::get_next_local_element_impl());
  }

  /// Gets the point to insert from the element to refine
  Point refinement_point_impl(const Facet& facet) const
  {
    CGAL_assertion (is_facet_on_surface(facet));
    set_last_vertex_index(get_facet_surface_center_index(facet));
    return get_facet_surface_center(facet);
  };

  /// Tests if p encroaches facet from zone
  // For sequential
  Mesher_level_conflict_status
  test_point_conflict_from_superior_impl(const Point& p, Zone& zone);

  /// Tests if p encroaches facet from zone
  // For parallel
  template <typename Mesh_visitor>
  Mesher_level_conflict_status
  test_point_conflict_from_superior_impl(const Point& p, Zone& zone,
                                         Mesh_visitor &visitor);

  /// Useless here
  Mesher_level_conflict_status private_test_point_conflict_impl(
      const Point& p,
      Zone& zone)
  {
    if( zone.locate_type == Tr::VERTEX )
    {
      std::stringstream sstr;
      sstr << "(" << p << ") is already inserted on surface.\n";
      CGAL_error_msg(sstr.str().c_str());
      return CONFLICT_AND_ELEMENT_SHOULD_BE_DROPPED;
    }
    else
      return NO_CONFLICT;
  }

  /// Returns the conflicts zone
  Zone conflicts_zone_impl(const Point& point
                           , const Facet& facet
                           , bool &facet_not_in_its_cz);
  Zone conflicts_zone_impl(const Point& point
                           , const Facet& facet
                           , bool &facet_not_in_its_cz
                           , bool &could_lock_zone);

  /// Job to do before insertion
  void before_insertion_impl(const Facet& facet,
                             const Point& point,
                             Zone& zone);

  /// Job to do after insertion
  void after_insertion_impl(const Vertex_handle& v)
  {
    restore_restricted_Delaunay(v);
  }

  /// Insert p into triangulation
  Vertex_handle insert_impl(const Point& p, const Zone& zone);

  /// Restore restricted Delaunay ; may be call by Cells_mesher visitor
  void restore_restricted_Delaunay(const Vertex_handle& v);

  /// debug info: class name
  std::string debug_info_class_name_impl() const
  {
    return "Refine_facets_3";
  }

  /// debug info
  std::string debug_info() const
  {
    std::stringstream s;
    s << Container_::size();
    return s.str();
  }

  /// debug_info_header
  std::string debug_info_header() const
  {
    return "#facets to refine";
  }

  std::string debug_info_element_impl(const Facet &facet) const
  {
    std::stringstream sstr;
    sstr << "Facet { " << std::endl
    << "  - " << *facet.first->vertex((facet.second+1)%4)  << std::endl
    << "  - " << *facet.first->vertex((facet.second+2)%4)  << std::endl
    << "  - " << *facet.first->vertex((facet.second+3)%4)  << std::endl
    << "  - 4th vertex in cell: " << *facet.first->vertex(facet.second)  << std::endl
    << "}" << std::endl;

    return sstr.str();
  }

#ifdef CGAL_MESH_3_MESHER_STATUS_ACTIVATED
  std::size_t queue_size() const { return this->size(); }
#endif


private:
  //-------------------------------------------------------
  // Private types
  //-------------------------------------------------------
  typedef typename Tr::Geom_traits Gt;
  typedef typename Tr::Cell_handle Cell_handle;
  typedef typename Criteria::Facet_quality Quality;
  typedef typename Get_Is_facet_bad<Criteria>::Type Is_facet_bad;
  typedef typename MeshDomain::Surface_patch_index Surface_patch_index;
  typedef typename MeshDomain::Index Index;
  typedef typename Gt::Segment_3 Segment_3;
  typedef typename Gt::Ray_3 Ray_3;
  typedef typename Gt::Line_3 Line_3;

  typedef typename boost::optional<CGAL::cpp0x::tuple<Surface_patch_index, Index, Point> >
                                                               Facet_properties;

private:
  /// Get mirror facet
  Facet mirror_facet(const Facet& f) const { return r_tr_.mirror_facet(f); };

  /// Returns canonical facet of facet
  Facet canonical_facet(const Facet& facet) const
  {
    const Facet mirror = mirror_facet(facet);
    return ( (facet < mirror)?facet:mirror );
  }

  /// Returns true if f has already been visited
  bool is_facet_visited(const Facet& f) const
  {
    return f.first->is_facet_visited(f.second);
  }

  /// Sets facet f to visited
  void set_facet_visited(Facet& f) const
  {
    f.first->set_facet_visited(f.second);
  }

  /// Sets facet f to not visited
  void reset_facet_visited(Facet& f) const
  {
    f.first->reset_visited(f.second);
  }

  /// Sets facet f and it's mirror one surface center to point p
  void set_facet_surface_center(const Facet& f,
                                const Point& p,
                                const Index& index) const
  {
    const Facet mirror = mirror_facet(f);

    f.first->set_facet_surface_center(f.second, p);
    mirror.first->set_facet_surface_center(mirror.second, p);

    f.first->set_facet_surface_center_index(f.second,index);
    mirror.first->set_facet_surface_center_index(mirror.second,index);
  }

  /// Returns facet surface center of \c f
  Point get_facet_surface_center(const Facet& f) const
  {
    return f.first->get_facet_surface_center(f.second);
  }

  /// Returns index of surface center of facet \c f
  Index get_facet_surface_center_index(const Facet& f) const
  {
    return f.first->get_facet_surface_center_index(f.second);
  }

  /// Sets \c f to surface facets, with index \c index
  void set_facet_on_surface(const Facet& f,
                            const Surface_patch_index& index)
  {
    r_c3t3_.add_to_complex(f, index);
  }

  /// Returns index of facet \c f
  Surface_patch_index get_facet_surface_index(const Facet& f) const
  {
    return r_c3t3_.surface_patch_index(f.first, f.second);
  }

  /// Removes \c f from surface facets
  void remove_facet_from_surface(const Facet& f)
  {
    r_c3t3_.remove_from_complex(f);
  }

  /// Returns to if f is on surface
  bool is_facet_on_surface(const Facet& f) const
  {
    return r_c3t3_.is_in_complex(f) ;
  }

  /// Sets index and dimension of vertex \v
  void set_vertex_properties(Vertex_handle& v, const Index& index)
  {
    r_c3t3_.set_index(v, index);
    // Set dimension of v: v is on surface by construction, so dimension=2
    v->set_dimension(2);
  }

  /// Computes facet properties and add facet to the refinement queue if needed
  void treat_new_facet(Facet& facet);

  /**
   * Computes at once is_facet_on_surface and facet_surface_center.
   * @param facet The input facet
   * @return \c true if \c facet is on surface, \c false otherwise
   */
  Facet_properties compute_facet_properties(
    const Facet& facet, bool force_exact = false) const;

  /// Returns true if point encroaches facet
  bool is_facet_encroached(const Facet& facet, const Point& point) const;

  /// Returns whethere an encroached facet is refinable or not
  bool is_encroached_facet_refinable(Facet& facet) const;

  /// Insert facet into refinement queue
  void insert_bad_facet(Facet& facet, const Quality& quality)
  {
    // Insert the facet and its mirror
    Facet mirror = mirror_facet(facet);
    this->add_bad_element(
      from_facet_to_refinement_queue_element(facet, mirror_facet(facet)),
      quality);
  }

  /// Insert encroached facet into refinement queue
  void insert_encroached_facet_in_queue(Facet& facet)
  {
    insert_bad_facet(facet,Quality());
  }

  /// Removes facet from refinement queue
  // Sequential
  void remove_bad_facet(Facet& facet, Sequential_tag)
  {
    // If sequential AND NOT lazy, remove cell from refinement queue
#if !defined(CGAL_MESH_3_USE_LAZY_SORTED_REFINEMENT_QUEUE) \
 && !defined(CGAL_MESH_3_USE_LAZY_UNSORTED_REFINEMENT_QUEUE)
    // Remove canonical facet
    Facet canonical_facet = this->canonical_facet(facet);
    this->remove_element(canonical_facet);
#endif
  }
#ifdef CGAL_LINKED_WITH_TBB
  /// Removes facet from refinement queue
  // Parallel: it's always lazy, so do nothing
  void remove_bad_facet(Facet& facet, Parallel_tag) {}
#endif // CGAL_LINKED_WITH_TBB

  /**
   * Action to perform on a facet inside the conflict zone before insertion
   * @return true if source_facet is the same as facet or mirror(facet)
   */
  bool before_insertion_handle_facet_in_conflict_zone(Facet& facet,
                                                     const Facet& source_facet);

  /**
   * Action to perform on a facet on the boundary of the conflict zone
   * before insertion
   * @return true if source_facet is the same as facet or mirror(facet)
   */
  bool before_insertion_handle_facet_on_conflict_zone(Facet& facet,
                                                      const Facet& source_facet)
  {
    // perform the same operations as for an internal facet
    return before_insertion_handle_facet_in_conflict_zone(facet, source_facet);
  }

  /// Action to perform on a facet incident to the new vertex
  void after_insertion_handle_incident_facet(Facet& facet);

  /// Action to perform on a facet opposite to the new vertex
  void after_insertion_handle_opposite_facet(Facet& facet)
  {
    // perform the same operations as for a facet incident to the new vertex
    after_insertion_handle_incident_facet(facet);
  }

private:
  /// The triangulation
  Tr& r_tr_;
  /// The facet criteria
  const Criteria& r_criteria_;
  /// The oracle
  const MeshDomain& r_oracle_;
  /// The mesh result
  C3T3& r_c3t3_;

private:
  // Disabled copy constructor
  Refine_facets_3(const Self& src);
  // Disabled assignment operator
  Self& operator=(const Self& src);
};  // end class Refine_facets_3




// For sequential
template<class Tr, class Cr, class MD, class C3T3_, class P_, class Ct, class C_>
Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::
Refine_facets_3(Tr& triangulation,
                const Cr& criteria,
                const MD& oracle,
                P_& previous,
                C3T3& c3t3)
  : Mesher_level<Tr, Self, Facet, P_,
      Triangulation_mesher_level_traits_3<Tr>, Ct>(previous)
  , C_()
  , No_after_no_insertion()
  , No_before_conflicts()
  , r_tr_(triangulation)
  , r_criteria_(criteria)
  , r_oracle_(oracle)
  , r_c3t3_(c3t3)
{

}

// For parallel
template<class Tr, class Cr, class MD, class C3T3_, class P_, class Ct, class C_>
Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::
Refine_facets_3(Tr& triangulation,
                const Cr& criteria,
                const MD& oracle,
                P_& previous,
                C3T3& c3t3,
                Mesh_3::LockDataStructureType *p_lock_ds,
                Mesh_3::WorksharingDataStructureType *p_worksharing_ds)
  : Mesher_level<Tr, Self, Facet, P_,
      Triangulation_mesher_level_traits_3<Tr>, Ct>(previous)
  , C_()
  , No_after_no_insertion()
  , No_before_conflicts()
  , r_tr_(triangulation)
  , r_criteria_(criteria)
  , r_oracle_(oracle)
  , r_c3t3_(c3t3)
{
  Base::set_lock_ds(p_lock_ds);
  Base::set_worksharing_ds(p_worksharing_ds);
}


template<class Tr, class Cr, class MD, class C3T3_, class P_, class Ct, class C_>
void
Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::
scan_triangulation_impl()
{
  typedef typename Tr::Finite_facets_iterator Finite_facet_iterator;

#ifdef MESH_3_PROFILING
  WallClockTimer t;
#endif

#ifdef CGAL_MESH_3_VERY_VERBOSE
  std::cerr
    << "Vertices: " << r_c3t3_.triangulation().number_of_vertices() << std::endl
    << "Facets  : " << r_c3t3_.number_of_facets_in_complex() << std::endl
    << "Tets    : " << r_c3t3_.number_of_cells_in_complex() << std::endl;
#endif

#ifdef CGAL_LINKED_WITH_TBB
  // Parallel
  if (boost::is_base_of<Parallel_tag, Ct>::value)
  {
    std::cerr << "Scanning triangulation for bad facets (in parallel) - "
      "number of finite facets = "
      << r_c3t3_.triangulation().number_of_finite_facets() << "...";
    add_to_TLS_lists(true);
    // PARALLEL_DO
    tbb::parallel_do(r_tr_.finite_facets_begin(), r_tr_.finite_facets_end(),
      [=]( const Facet &facet ) { // CJTODO: lambdas ok?
        // Cannot be const, see treat_new_facet signature
        Facet f = facet;
        this->treat_new_facet( f );
    });
    splice_local_lists();
    add_to_TLS_lists(false);
  }
  // Sequential
  else
#endif // CGAL_LINKED_WITH_TBB
  {
    std::cerr << "Scanning triangulation for bad facets (sequential) - "
      "number of finite facets = " 
      << r_c3t3_.triangulation().number_of_finite_facets() << "...";
    for(Finite_facet_iterator facet_it = r_tr_.finite_facets_begin();
        facet_it != r_tr_.finite_facets_end();
        ++facet_it)
    {
      // Cannot be const, see treat_new_facet signature
      Facet facet = *facet_it;
      /*std::cerr << "*" << *facet.first->vertex((facet.second+1)%4)  << std::endl
          << "  " << *facet.first->vertex((facet.second+2)%4)  << std::endl
          << "  " << *facet.first->vertex((facet.second+3)%4)  << std::endl;*/
      treat_new_facet(facet);
    }
  }

#ifdef MESH_3_PROFILING
  std::cerr << "scan done in " << t.elapsed() << " seconds." << std::endl;
#endif
  
  std::cerr << "Number of bad facets: " << C_::size() << std::endl;
  
#ifdef MESH_3_PROFILING
  std::cerr << "Refining... ";
  Base_ML::m_timer.reset();
#endif
}



template<class Tr, class Cr, class MD, class C3T3_, class P_, class Ct, class C_>
int
Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::
get_number_of_bad_elements_impl()
{
  typedef typename Tr::Finite_facets_iterator Finite_facet_iterator;

  int count = 0, count_num_bad_surface_facets = 0;
  int num_internal_facets_that_should_be_on_surface = 0;
  std::cerr << "Scanning triangulation for bad facets - "
    "number of finite facets = " 
    << r_c3t3_.triangulation().number_of_finite_facets() << "...";
  int num_tested_facets = 0;
  for(Finite_facet_iterator facet_it = r_tr_.finite_facets_begin();
      facet_it != r_tr_.finite_facets_end();
      ++facet_it)
  {
    Facet facet = *facet_it;
    Facet_properties properties = compute_facet_properties(facet);
    
#ifdef SHOW_REMAINING_BAD_ELEMENT_IN_RED
    //facet.first->mark = 0;
#endif

    // On surface?
    if ( properties )
    {
      // This facet should be on surface...
      if (!is_facet_on_surface(facet))
      {
        std::cerr << "\n\n*** The facet f is on surface but is NOT MARKED ON SURFACE. " << std::endl;
        
        // CJTODO DEBUG
        Cell_handle c = facet.first;
        int ind = facet.second;
        Cell_handle mc = mirror_facet(facet).first;
        int mind = mirror_facet(facet).second;
        
#ifdef SHOW_REMAINING_BAD_ELEMENT_IN_RED
        c->mark2 = ind;
#endif

        // c and mc are the cells adjacent to f
        // browse each facet ff of c and mc, and mark it if it is "on surface"
        int num_erroneous_surface_facets_in_c = 0;
        int num_erroneous_surface_facets_in_mc = 0;
        int num_real_surface_facets_in_c = 0;
        int num_real_surface_facets_in_mc = 0;
        for (int i = 0 ; i < 4 ; ++i)
        {
          if (i != ind)
          {
            const Facet f1(c, i);
            if (is_facet_on_surface(f1))
            {
              std::cerr << "*** f1 is " << (r_criteria_(f1) ? "bad" : "good") << std::endl;

#ifdef SHOW_REMAINING_BAD_ELEMENT_IN_RED
              c->mark = i;
#endif
              if (compute_facet_properties(f1))
                ++num_real_surface_facets_in_c;
              else
                ++num_erroneous_surface_facets_in_c;
            }
          }
          if (i != mind)
          {
            const Facet f2(c, i);
            if (is_facet_on_surface(f2))
            {
              std::cerr << "*** f2 is " << (r_criteria_(f2) ? "bad" : "good") << std::endl;

#ifdef SHOW_REMAINING_BAD_ELEMENT_IN_RED
              mc->mark = i;
#endif
              if (compute_facet_properties(f2))
                ++num_real_surface_facets_in_mc;
              else
                ++num_erroneous_surface_facets_in_mc;
            }
          }
        }

        std::cerr 
          << "*** Num of erroneous surface facets in c: " << num_erroneous_surface_facets_in_c << std::endl
          << "*** Num of erroneous surface facets in mc: " << num_erroneous_surface_facets_in_mc << std::endl
          << "*** Num of real surface facets in c: " << num_real_surface_facets_in_c << std::endl
          << "*** Num of real surface facets in mc: " << num_real_surface_facets_in_mc << std::endl;

        typedef typename MD::Subdomain Subdomain;
        const bool is_c_in_domain = r_oracle_.is_in_domain_object()(r_tr_.dual(c));
        const bool is_mc_in_domain = r_oracle_.is_in_domain_object()(r_tr_.dual(mc));
        
        std::cerr << "*** Is in complex? c is marked in domain: " << r_c3t3_.is_in_complex(c) 
          << " / c is really in domain: " << is_c_in_domain
          << " / mc is marked in domain: " << r_c3t3_.is_in_complex(mc)
          << " / mc is really in domain: " << is_mc_in_domain
          << std::endl;
        

        // ... if it's not, count it
        ++num_internal_facets_that_should_be_on_surface;

      }
      
      const Surface_patch_index& surface_index = CGAL::cpp0x::get<0>(*properties);
      const Index& surface_center_index = CGAL::cpp0x::get<1>(*properties);
      const Point& surface_center = CGAL::cpp0x::get<2>(*properties);

      // Facet is on surface: set facet properties
      //set_facet_surface_center(facet, surface_center, surface_center_index);
      //set_facet_on_surface(facet, surface_index);

      const Is_facet_bad is_facet_bad = r_criteria_(facet);
      if ( is_facet_bad )
      {
        ++count;
        if (is_facet_on_surface(facet))
          ++count_num_bad_surface_facets;

#ifdef SHOW_REMAINING_BAD_ELEMENT_IN_RED
        //facet.first->mark = facet.second;
#endif
      }
      ++ num_tested_facets;
    }
    // Not on surface?
    else
    {
      // Facet is not on surface
      //remove_facet_from_surface(facet);

      // Marked on surface?
      if (is_facet_on_surface(facet))
      {
        std::cerr << "************** The facet is marked on surface whereas it's not! **************" << std::endl;
#ifdef SHOW_REMAINING_BAD_ELEMENT_IN_RED
        facet.first->mark = facet.second;
#endif
      }
    }
  }

  /*std::cerr << "done (" << num_internal_facets_that_should_be_on_surface 
    << " facets which were internal facets were added to the surface)." << std::endl;*/
  std::cerr << "done (" << num_internal_facets_that_should_be_on_surface 
    << " facets that should be on surface are actually internal facets)." << std::endl;
  std::cerr << std::endl << "Num_tested_facets = " << num_tested_facets << std::endl;
  std::cerr << std::endl << "Num bad surface-marked facets = " << count_num_bad_surface_facets << std::endl;

  return count;
}

// For sequential
template<class Tr, class Cr, class MD, class C3T3_, class P_, class Ct, class C_>
Mesher_level_conflict_status
Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::
test_point_conflict_from_superior_impl(const Point& point, Zone& zone)
{
  typedef typename Zone::Facets_iterator Facet_iterator;

  for (Facet_iterator facet_it = zone.internal_facets.begin();
       facet_it != zone.internal_facets.end();
       ++facet_it)
  {
    // surface facets which are internal facets of the conflict zone are
    // encroached
    if( is_facet_on_surface(*facet_it) )
    {
      if ( is_encroached_facet_refinable(*facet_it) )
      {
        insert_encroached_facet_in_queue(*facet_it);
        return CONFLICT_BUT_ELEMENT_CAN_BE_RECONSIDERED;
      }
      else
        return CONFLICT_AND_ELEMENT_SHOULD_BE_DROPPED;
    }
  }

  for (Facet_iterator facet_it = zone.boundary_facets.begin();
       facet_it != zone.boundary_facets.end();
       ++facet_it)
  {
    if( is_facet_encroached(*facet_it, point) )
    {
      // Insert already existing surface facet into refinement queue
      if ( is_encroached_facet_refinable(*facet_it) )
      {
        insert_encroached_facet_in_queue(*facet_it);
        return CONFLICT_BUT_ELEMENT_CAN_BE_RECONSIDERED;
      }
      else
        return CONFLICT_AND_ELEMENT_SHOULD_BE_DROPPED;
    }
  }

  return NO_CONFLICT;
}

// For parallel
template<class Tr, class Cr, class MD, class C3T3_, class P_, class Ct, class C_>
template <typename Mesh_visitor>
Mesher_level_conflict_status
Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::
test_point_conflict_from_superior_impl(const Point& point, Zone& zone,
                                       Mesh_visitor &visitor)
{
  typedef typename Zone::Facets_iterator Facet_iterator;

  for (Facet_iterator facet_it = zone.internal_facets.begin();
       facet_it != zone.internal_facets.end();
       ++facet_it)
  {
    // surface facets which are internal facets of the conflict zone are
    // encroached
    if( is_facet_on_surface(*facet_it) )
    {
      if ( is_encroached_facet_refinable(*facet_it) )
      {
        // Even if it doesn't succeed, it will be tried again
        try_to_refine_element(*facet_it, visitor);
        return CONFLICT_BUT_ELEMENT_CAN_BE_RECONSIDERED;
      }
      else
        return CONFLICT_AND_ELEMENT_SHOULD_BE_DROPPED;
    }
  }

  for (Facet_iterator facet_it = zone.boundary_facets.begin();
       facet_it != zone.boundary_facets.end();
       ++facet_it)
  {
    if( is_facet_encroached(*facet_it, point) )
    {
      // Insert already existing surface facet into refinement queue
      if ( is_encroached_facet_refinable(*facet_it) )
      {
        // Even if it doesn't succeed, it will be tried again
        try_to_refine_element(*facet_it, visitor);
        return CONFLICT_BUT_ELEMENT_CAN_BE_RECONSIDERED;
      }
      else
        return CONFLICT_AND_ELEMENT_SHOULD_BE_DROPPED;
    }
  }

  return NO_CONFLICT;
}


template<class Tr, class Cr, class MD, class C3T3_, class P_, class Ct, class C_>
typename Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::Zone
Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::
conflicts_zone_impl(const Point& point
                    , const Facet& facet
                    , bool &facet_not_in_its_cz)
{
  Zone zone;

  // TODO may be avoid the locate here
  zone.cell = r_tr_.locate(point,
                           zone.locate_type,
                           zone.i,
                           zone.j,
                           facet.first);

  if(zone.locate_type != Tr::VERTEX)
  {
    const Facet *p_facet = (facet == Facet() ? 0 : &facet);

    r_tr_.find_conflicts(point,
                         zone.cell,
                         std::back_inserter(zone.boundary_facets),
                         std::back_inserter(zone.cells),
                         std::back_inserter(zone.internal_facets)
                         , 0
                         , p_facet
                         , &facet_not_in_its_cz);

    if (p_facet != 0 && facet_not_in_its_cz)
    {
# ifdef CGAL_MESH_3_VERBOSE
      std::cerr << "Info: the facet is not in its conflict zone. "
        "Switching to exact computation." << std::endl;
# endif

      Facet_properties properties = compute_facet_properties(
                                    facet, /*force_exact=*/true);
      if ( properties )
      {
        const Surface_patch_index& surface_index = CGAL::cpp0x::get<0>(*properties);
        const Index& surface_center_index = CGAL::cpp0x::get<1>(*properties);
        const Point& surface_center = CGAL::cpp0x::get<2>(*properties);

        // Facet is on surface: set facet properties
        set_facet_surface_center(facet, surface_center, surface_center_index);
        set_facet_on_surface(facet, surface_index);
      }
      else
      {
        // Facet is not on surface
        remove_facet_from_surface(facet);
      }
    }
  }

  return zone;
}

template<class Tr, class Cr, class MD, class C3T3_, class P_, class Ct, class C_>
typename Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::Zone
Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::
conflicts_zone_impl(const Point& point
                    , const Facet& facet
                    , bool &facet_not_in_its_cz
                    , bool &could_lock_zone)
{
  Zone zone;

  // TODO may be avoid the locate here
  zone.cell = r_tr_.locate(point,
                           zone.locate_type,
                           zone.i,
                           zone.j,
                           facet.first,
                           &could_lock_zone);

  if(could_lock_zone && zone.locate_type != Tr::VERTEX)
  {
    const Facet *p_facet = (facet == Facet() ? 0 : &facet);

    r_tr_.find_conflicts(point,
                         zone.cell,
                         std::back_inserter(zone.boundary_facets),
                         std::back_inserter(zone.cells),
                         std::back_inserter(zone.internal_facets)
                         , &could_lock_zone
                         , p_facet
                         , &facet_not_in_its_cz);

    if (could_lock_zone && p_facet != 0 && facet_not_in_its_cz)
    {
#ifdef CGAL_MESH_3_VERBOSE
      std::cerr << "Info: the facet is not in its conflict zone. "
        "Switching to exact computation." << std::endl;
#endif

      Facet_properties properties = compute_facet_properties(
                                    facet, /*force_exact=*/true);
      if ( properties )
      {
        const Surface_patch_index& surface_index = CGAL::cpp0x::get<0>(*properties);
        const Index& surface_center_index = CGAL::cpp0x::get<1>(*properties);
        const Point& surface_center = CGAL::cpp0x::get<2>(*properties);

        // Facet is on surface: set facet properties
        set_facet_surface_center(facet, surface_center, surface_center_index);
        set_facet_on_surface(facet, surface_index);
      }
      else
      {
        // Facet is not on surface
        remove_facet_from_surface(facet);
      }
    }
  }

  return zone;
}



template<class Tr, class Cr, class MD, class C3T3_, class P_, class Ct, class C_>
void
Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::
before_insertion_impl(const Facet& facet,
                      const Point& point,
                      Zone& zone)
{
  typedef typename Zone::Facets_iterator Facets_iterator;

  bool source_facet_is_in_conflict = false;

  /*std::cerr << "before_insertion_impl:" << std::endl
    << "* " << *facet.first->vertex((facet.second+1)%4)  << std::endl
    << "  " << *facet.first->vertex((facet.second+2)%4)  << std::endl
    << "  " << *facet.first->vertex((facet.second+3)%4)  << std::endl;*/

  // Iterate on conflict zone facets
  for (Facets_iterator facet_it = zone.internal_facets.begin();
       facet_it != zone.internal_facets.end();
       ++facet_it)
  {
    if (before_insertion_handle_facet_in_conflict_zone(*facet_it, facet) )
    {
      source_facet_is_in_conflict = true;
    }
  }

  for (Facets_iterator facet_it = zone.boundary_facets.begin() ;
       facet_it != zone.boundary_facets.end() ;
       ++facet_it)
  {
    if (before_insertion_handle_facet_on_conflict_zone(*facet_it, facet))
    {
      source_facet_is_in_conflict = true;
    }
  }

  // source_facet == Facet() when this->before_insertion_impl is
  // called from a Mesh_3 visitor.
  if ( !source_facet_is_in_conflict && facet != Facet()  )
  {
    const Facet source_other_side = mirror_facet(facet);
    std::stringstream error_msg;
    error_msg <<
      boost::format("Mesh_3 ERROR: "
                    "A facet is not in conflict with its refinement point!\n"
                    "Debugging informations:\n"
                    "  Facet: (%1%, %2%) = (%6%, %7%, %8%)\n"
                    "  Dual: (%3%, %4%)\n"
                    "  Refinement point: %5%\n"
                    "  Cells adjacent to facet:\n"
                    "    ( %9% , %10% , %11% , %12% )\n"
                    "    ( %13% , %14% , %15% , %16% )\n")
      % (&*facet.first)
      % facet.second
      % triangulation_ref_impl().dual(facet.first)
      % triangulation_ref_impl().dual(source_other_side.first)
      % point
      % facet.first->vertex((facet.second + 1)&3)->point()
      % facet.first->vertex((facet.second + 2)&3)->point()
      % facet.first->vertex((facet.second + 3)&3)->point()
      % facet.first->vertex(0)->point()
      % facet.first->vertex(1)->point()
      % facet.first->vertex(2)->point()
      % facet.first->vertex(3)->point()
      % source_other_side.first->vertex(0)->point()
      % source_other_side.first->vertex(1)->point()
      % source_other_side.first->vertex(2)->point()
      % source_other_side.first->vertex(3)->point();

    CGAL_error_msg(error_msg.str().c_str());
  }
}



template<class Tr, class Cr, class MD, class C3T3_, class P_, class Ct, class C_>
typename Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::Vertex_handle
Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::
insert_impl(const Point& point,
            const Zone& zone)
{
  if( zone.locate_type == Tr::VERTEX )
  {
    // TODO look at this
    std::cerr<<"VERTEX\n";
    return zone.cell->vertex(zone.i);
  }

  const Facet& facet = *(zone.boundary_facets.begin());

  Vertex_handle v = r_tr_.insert_in_hole(point,
                                         zone.cells.begin(),
                                         zone.cells.end(),
                                         facet.first,
                                         facet.second);

  // Set index and dimension of v
  set_vertex_properties(v, Base::get_last_vertex_index());

  return v;
}



template<class Tr, class Cr, class MD, class C3T3_, class P_, class Ct, class C_>
void
Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::
restore_restricted_Delaunay(const Vertex_handle& vertex)
{
  typedef std::vector<Cell_handle> Cell_handle_vector;
  typedef typename Cell_handle_vector::iterator Cell_handle_vector_iterator;

  // Update incident facets
  Cell_handle_vector cells;
  r_tr_.incident_cells(vertex, std::back_inserter(cells));

  for(Cell_handle_vector_iterator cell_it = cells.begin();
      cell_it != cells.end();
      ++cell_it)
  {
    // Look at all four facets of the cell, starting with the
    // facet opposite to the new vertex
    int index = (*cell_it)->index(vertex);

    Facet opposite_facet(*cell_it, index);
    after_insertion_handle_opposite_facet(opposite_facet);

    for (int i = 1; i <= 3; ++i)
    {
      Facet incident_facet(*cell_it, (index+i)&3);
      after_insertion_handle_incident_facet(incident_facet);
    }
  }
}

//-------------------------------------------------------
// Private methods
//-------------------------------------------------------
template<class Tr, class Cr, class MD, class C3T3_, class P_, class Ct, class C_>
void
Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::
treat_new_facet(Facet& facet)
{
  // Treat facet
  Facet_properties properties = compute_facet_properties(facet);
  if ( properties )
  {
    const Surface_patch_index& surface_index = CGAL::cpp0x::get<0>(*properties);
    const Index& surface_center_index = CGAL::cpp0x::get<1>(*properties);
    const Point& surface_center = CGAL::cpp0x::get<2>(*properties);

    // Facet is on surface: set facet properties
    set_facet_surface_center(facet, surface_center, surface_center_index);
    set_facet_on_surface(facet, surface_index);

    // Insert facet into refinement queue if needed
    const Is_facet_bad is_facet_bad = r_criteria_(facet);

    if ( is_facet_bad )
    {
      insert_bad_facet(facet, *is_facet_bad);

      /*std::cerr << "INSERT BAD FACET : " << std::endl
        << "* " << *facet.first->vertex((facet.second+1)%4)  << std::endl
        << "  " << *facet.first->vertex((facet.second+2)%4)  << std::endl
        << "  " << *facet.first->vertex((facet.second+3)%4)  << std::endl
        << "  Quality=" << is_facet_bad->second << std::endl;*/
    }
  }
  else
  {
    // Facet is not on surface
    remove_facet_from_surface(facet);
  }

  // Set facet visited
  Facet mirror = mirror_facet(facet);
  set_facet_visited(facet);
  set_facet_visited(mirror);
}


template<class Tr, class Cr, class MD, class C3T3_, class P_, class Ct, class C_>
typename Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::Facet_properties
Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::
compute_facet_properties(const Facet& facet, bool force_exact) const
{
  //-------------------------------------------------------
  // Facet must be finite
  //-------------------------------------------------------
  CGAL_assertion( ! r_tr_.is_infinite(facet) );

  // types
  typedef typename MD::Surface_patch Surface_patch;
  typedef typename MD::Intersection Intersection;

  // Functor
  typename Gt::Is_degenerate_3 is_degenerate = Gt().is_degenerate_3_object();
  typename MD::Do_intersect_surface do_intersect_surface =
      r_oracle_.do_intersect_surface_object();


  // Get dual of facet
  Object dual = r_tr_.dual(facet, force_exact);

  // If the dual is a segment
  if ( const Segment_3* p_segment = object_cast<Segment_3>(&dual) )
  {
    if (is_degenerate(*p_segment)) { return Facet_properties(); }

    // If facet is on surface, compute intersection point and return true
    Surface_patch surface = do_intersect_surface(*p_segment);
    if ( surface )
    {
      typename MD::Construct_intersection construct_intersection =
          r_oracle_.construct_intersection_object();

      // Trick to have canonical vector : thus, we compute alwais the same
      // intersection
      Segment_3 segment = *p_segment;
      if ( CGAL::compare_xyz(p_segment->source(),p_segment->target())
              == CGAL::LARGER )
      {
        typename Gt::Construct_opposite_segment_3 opposite =
            Gt().construct_opposite_segment_3_object();

        segment = opposite(*p_segment);
      }

      Intersection intersect = construct_intersection(segment);
      return Facet_properties(CGAL::cpp0x::make_tuple(*surface,
                                                CGAL::cpp0x::get<1>(intersect),
                                                CGAL::cpp0x::get<0>(intersect)));
    }
  }
  // If the dual is a ray
  else if ( const Ray_3* p_ray = object_cast<Ray_3>(&dual) )
  {
    // If a facet is on the convex hull, and if its finite incident
    // cell has a very big Delaunay ball, then the dual of the facet is
    // a ray constructed with a point with very big coordinates, and a
    // vector with small coordinates. Its can happen than the
    // constructed ray is degenerate (the point(1) of the ray is
    // point(0) plus a vector whose coordinates are espilon).
    if (is_degenerate(*p_ray)) { return Facet_properties(); }

    Surface_patch surface = do_intersect_surface(*p_ray);
    if ( surface )
    {
      typename MD::Construct_intersection construct_intersection =
          r_oracle_.construct_intersection_object();

      Intersection intersect = construct_intersection(*p_ray);
      return Facet_properties(CGAL::cpp0x::make_tuple(*surface,
                                                CGAL::cpp0x::get<1>(intersect),
                                                CGAL::cpp0x::get<0>(intersect)));
    }
  }
  // If the dual is a line
  else if ( const Line_3* p_line = object_cast<Line_3>(&dual) )
  {
    Surface_patch surface = do_intersect_surface(*p_line);
    if ( surface )
    {
      typename MD::Construct_intersection construct_intersection =
          r_oracle_.construct_intersection_object();

      // Trick to have canonical vector : thus, we compute alwais the same
      // intersection
      Line_3 line = *p_line;
      if ( CGAL::compare_xyz(p_line->point(0),p_line->point(1))
              == CGAL::LARGER )
      {
        typename Gt::Construct_opposite_line_3 opposite =
            Gt().construct_opposite_line_3_object();

        line = opposite(*p_line);
      }

      Intersection intersect = construct_intersection(line);
      return Facet_properties(CGAL::cpp0x::make_tuple(*surface,
                                                CGAL::cpp0x::get<1>(intersect),
                                                CGAL::cpp0x::get<0>(intersect)));
    }
  }
  else
  {
    // Else there is a problem with the dual
    std::cerr << "In is_facet_on_surface(const Facet& f, Point& center)\n"
    << "file " << __FILE__ << ", line " << __LINE__ << "\n";
    std::cerr << "Incorrect object type: " << dual.type().name() << "\n";
    CGAL_error();
  }

  return Facet_properties();
}


template<class Tr, class Cr, class MD, class C3T3_, class P_, class Ct, class C_>
bool
Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::
is_facet_encroached(const Facet& facet,
                    const Point& point) const
{
  if ( r_tr_.is_infinite(facet) || ! is_facet_on_surface(facet) )
  {
    return false;
  }

  typename Gt::Compare_power_distance_3 compare_distance =
    r_tr_.geom_traits().compare_power_distance_3_object();

  const Cell_handle& cell = facet.first;
  const int& facet_index = facet.second;
  const Point& center = get_facet_surface_center(facet);
  const Point& reference_point = cell->vertex((facet_index+1)&3)->point();

  // facet is encroached if the new point is near from center than
  // one vertex of the facet
  return ( compare_distance(center, reference_point, point) != CGAL::SMALLER );
}

template<class Tr, class Cr, class MD, class C3T3_, class P_, class Ct, class C_>
bool
Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::
is_encroached_facet_refinable(Facet& facet) const
{
  typedef typename Gt::Point_3 Point_3;
  typedef typename Gt::FT      FT;

  typename Gt::Compute_squared_radius_smallest_orthogonal_sphere_3 sq_radius =
    Gt().compute_squared_radius_smallest_orthogonal_sphere_3_object();

  typename Gt::Compare_weighted_squared_radius_3 compare =
    Gt().compare_weighted_squared_radius_3_object();

  const Cell_handle& c = facet.first;
  const int& k = facet.second;

  int k1 = (k+1)&3;
  int k2 = (k+2)&3;
  int k3 = (k+3)&3;

  // Get number of weighted points, and ensure that they will be accessible
  // using k1...ki, if i is the number of weighted points.
  int wp_nb = 0;
  if(c->vertex(k1)->point().weight() > FT(0))
  {
    ++wp_nb;
  }

  if(c->vertex(k2)->point().weight() > FT(0))
  {
    if ( 0 == wp_nb ) { std::swap(k1,k2); }
    ++wp_nb;
  }

  if(c->vertex(k3)->point().weight() > FT(0))
  {
    if ( 0 == wp_nb ) { std::swap(k1,k3); }
    if ( 1 == wp_nb ) { std::swap(k2,k3); }
    ++wp_nb;
  }

  const Point_3& p1 = c->vertex(k1)->point();
  const Point_3& p2 = c->vertex(k2)->point();
  const Point_3& p3 = c->vertex(k3)->point();

  const FT min_ratio (0.16); // (0.2*2)^2

  // Check ratio
  switch ( wp_nb )
  {
    case 1:
    {
      FT r = (std::max)(sq_radius(p1,p2),sq_radius(p1,p3));
      if ( r < min_ratio*p1.weight() ) { return false; }
      break;
    }

    case 2:
    {
      bool do_spheres_intersect = (compare(p1,p2,FT(0)) != CGAL::LARGER);
      if ( do_spheres_intersect )
      {
        FT r13 = sq_radius(p1,p3) / p1.weight();
        FT r23 = sq_radius(p2,p3) / p2.weight();
        FT r = (std::max)(r13,r23);

        if ( r < min_ratio ) { return false; }
      }
      break;
    }

    case 3:
    {
      bool do_spheres_intersect = (compare(p1,p2,p3,FT(0)) != CGAL::LARGER);
      if ( do_spheres_intersect ) { return false; }
      break;
    }

    default: break;
  }

  return true;
}

/**
  * \c facet is an internal facet we are going to remove
  * \c source_facet is the facet we want to refine by inserting a new point
  */
template<class Tr, class Cr, class MD, class C3T3_, class P_, class Ct, class C_>
bool
Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::
before_insertion_handle_facet_in_conflict_zone(Facet& facet,
                                               const Facet& source_facet)
{
  Facet other_side = mirror_facet(facet);

  // Is the facet on the surface of the complex
  if ( is_facet_on_surface(facet) )
  {
    // Remove element (if needed - see remove_bad_facet implementation)
    remove_bad_facet(facet, Ct());

    // Remove facet from complex
    remove_facet_from_surface(facet);

    // Reset visited
    reset_facet_visited(facet);
    reset_facet_visited(other_side);
  }

  return ( (facet == source_facet) || (other_side == source_facet) );
}



template<class Tr, class Cr, class MD, class C3T3_, class P_, class Ct, class C_>
void
Refine_facets_3<Tr,Cr,MD,C3T3_,P_,Ct,C_>::
after_insertion_handle_incident_facet(Facet& facet)
{
  // If the facet is infinite or has been already visited,
  // then there is nothing to do as for it or its edges
  // Facet other_side = mirror_facet(facet);
  if ( r_tr_.is_infinite(facet) || (is_facet_visited(facet)) ) // && is_facet_visited(other_side)) )
  {
    return;
  }

  treat_new_facet(facet);
}


}  // end namespace Mesh_3


}  // end namespace CGAL

#endif // CGAL_MESH_3_REFINE_FACETS_3_H
