
/*

  KLayout Layout Viewer
  Copyright (C) 2006-2019 Matthias Koefferlein

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/


#include "dbDeepShapeStore.h"
#include "dbCellMapping.h"
#include "dbLayoutUtils.h"
#include "dbRegion.h"
#include "dbDeepRegion.h"

#include "tlTimer.h"

namespace db
{

// ----------------------------------------------------------------------------------

DeepLayer::DeepLayer ()
  : mp_store (), m_layout (0), m_layer (0)
{
  //  .. nothing yet ..
}

DeepLayer::DeepLayer (const Region &region)
  : mp_store (), m_layout (0), m_layer (0)
{
  const db::DeepRegion *dr = dynamic_cast<db::DeepRegion *> (region.delegate ());
  tl_assert (dr != 0);
  *this = dr->deep_layer ();
}

DeepLayer::DeepLayer (const DeepLayer &x)
  : mp_store (x.mp_store), m_layout (x.m_layout), m_layer (x.m_layer)
{
  if (mp_store.get ()) {
    mp_store->add_ref (m_layout, m_layer);
  }
}

DeepLayer::DeepLayer (DeepShapeStore *store, unsigned int layout, unsigned int layer)
  : mp_store (store), m_layout (layout), m_layer (layer)
{
  if (store) {
    store->add_ref (layout, layer);
  }
}

DeepLayer &DeepLayer::operator= (const DeepLayer &other)
{
  if (this != &other) {
    if (mp_store.get ()) {
      mp_store->remove_ref (m_layout, m_layer);
    }
    mp_store = other.mp_store;
    m_layout = other.m_layout;
    m_layer = other.m_layer;
    if (mp_store.get ()) {
      mp_store->add_ref (m_layout, m_layer);
    }
  }

  return *this;
}


DeepLayer::~DeepLayer ()
{
  if (mp_store.get ()) {
    mp_store->remove_ref (m_layout, m_layer);
  }
}

DeepLayer
DeepLayer::derived () const
{
  return DeepLayer (const_cast <db::DeepShapeStore *> (mp_store.get ()), m_layout, const_cast <db::Layout &> (layout ()).insert_layer ());
}

DeepLayer
DeepLayer::copy () const
{
  DeepLayer new_layer (derived ());

  db::DeepShapeStore *non_const_store = const_cast<db::DeepShapeStore *> (mp_store.get ());
  non_const_store->layout (m_layout).copy_layer (m_layer, new_layer.layer ());

  return new_layer;
}

void
DeepLayer::add_from (const DeepLayer &dl)
{
  if (&dl.layout () == &layout ()) {

    //  intra-layout merge

    layout ().copy_layer (dl.layer (), layer ());

  } else {

    //  inter-layout merge

    db::cell_index_type into_cell = initial_cell ().cell_index ();
    db::Layout *into_layout = &layout ();
    db::cell_index_type source_cell = dl.initial_cell ().cell_index ();
    const db::Layout *source_layout = &dl.layout ();

    db::CellMapping cm;
    cm.create_from_geometry_full (*into_layout, into_cell, *source_layout, source_cell);

    //  Actually copy the shapes

    std::map<unsigned int, unsigned int> lm;
    lm.insert (std::make_pair (dl.layer (), layer ()));

    std::vector <db::cell_index_type> source_cells;
    source_cells.push_back (source_cell);
    db::copy_shapes (*into_layout, *source_layout, db::ICplxTrans (), source_cells, cm.table (), lm);

  }
}

void
DeepLayer::insert_into (db::Layout *into_layout, db::cell_index_type into_cell, unsigned int into_layer) const
{
  check_dss ();
  const_cast<db::DeepShapeStore *> (mp_store.get ())->insert (*this, into_layout, into_cell, into_layer);
}

void
DeepLayer::insert_into_as_polygons (db::Layout *into_layout, db::cell_index_type into_cell, unsigned int into_layer, db::Coord enl) const
{
  check_dss ();
  const_cast<db::DeepShapeStore *> (mp_store.get ())->insert_as_polygons (*this, into_layout, into_cell, into_layer, enl);
}

bool DeepLayer::operator< (const DeepLayer &other) const
{
  if (mp_store.get () != other.mp_store.get ()) {
    return mp_store.get () < other.mp_store.get ();
  }
  if (m_layout != other.m_layout) {
    return m_layout < other.m_layout;
  }
  if (m_layer != other.m_layer) {
    return m_layer < other.m_layer;
  }
  return false;
}

bool DeepLayer::operator== (const DeepLayer &other) const
{
  if (mp_store.get () != other.mp_store.get ()) {
    return false;
  }
  if (m_layout != other.m_layout) {
    return false;
  }
  if (m_layer != other.m_layer) {
    return false;
  }
  return true;
}

db::Layout &
DeepLayer::layout ()
{
  check_dss ();
  return mp_store->layout (m_layout);
}

const db::Layout &
DeepLayer::layout () const
{
  check_dss ();
  return const_cast<db::DeepShapeStore *> (mp_store.get ())->layout (m_layout);
}

db::Cell &
DeepLayer::initial_cell ()
{
  check_dss ();
  return mp_store->initial_cell (m_layout);
}

const db::Cell &
DeepLayer::initial_cell () const
{
  check_dss ();
  return mp_store->const_initial_cell (m_layout);
}

void
DeepLayer::check_dss () const
{
  if (mp_store.get () == 0) {
    throw tl::Exception (tl::to_string (tr ("Heap lost: the DeepShapeStore container no longer exists")));
  }
}

// ----------------------------------------------------------------------------------

struct DeepShapeStore::LayoutHolder
{
  LayoutHolder (const db::ICplxTrans &trans)
    : refs (0), layout (false), builder (&layout, trans)
  {
    //  .. nothing yet ..
  }

  void add_layer_ref (unsigned int layer)
  {
    layer_refs[layer] += 1;
  }

  void remove_layer_ref (unsigned int layer)
  {
    if ((layer_refs[layer] -= 1) <= 0) {
      layout.delete_layer (layer);
      layer_refs.erase (layer);
    }
  }

  int refs;
  db::Layout layout;
  db::HierarchyBuilder builder;
  std::map<unsigned int, int> layer_refs;
};

// ----------------------------------------------------------------------------------

static size_t s_instance_count = 0;

DeepShapeStore::DeepShapeStore ()
  : m_threads (1), m_max_area_ratio (3.0), m_max_vertex_count (16), m_text_property_name (), m_text_enlargement (-1)
{
  ++s_instance_count;
}

DeepShapeStore::~DeepShapeStore ()
{
  --s_instance_count;

  for (std::vector<LayoutHolder *>::iterator h = m_layouts.begin (); h != m_layouts.end (); ++h) {
    delete *h;
  }
  m_layouts.clear ();
}

bool DeepShapeStore::is_singular () const
{
  return m_layouts.size () == 1;
}

void DeepShapeStore::require_singular () const
{
  if (! is_singular ()) {
    throw tl::Exception (tl::to_string (tr ("Internal error: deep shape store isn't singular. This may happen if you try to mix hierarchical layers from different sources our you use clipping.")));
  }
}

Cell &DeepShapeStore::initial_cell(unsigned int n)
{
  db::Layout &ly = layout (n);
  tl_assert (ly.cells () > 0);
  return ly.cell (*ly.begin_top_down ());
}

const db::Cell &DeepShapeStore::const_initial_cell (unsigned int n) const
{
  const db::Layout &ly = const_layout (n);
  tl_assert (ly.cells () > 0);
  return ly.cell (*ly.begin_top_down ());
}

void DeepShapeStore::set_text_enlargement (int enl)
{
  m_text_enlargement = enl;
}

void DeepShapeStore::set_text_property_name (const tl::Variant &pn)
{
  m_text_property_name = pn;
}

bool DeepShapeStore::is_valid_layout_index (unsigned int n) const
{
  return (n < (unsigned int) m_layouts.size () && m_layouts[n] != 0);
}

const db::Layout &DeepShapeStore::const_layout (unsigned int n) const
{
  tl_assert (is_valid_layout_index (n));
  return m_layouts [n]->layout;
}

db::Layout &DeepShapeStore::layout (unsigned int n)
{
  tl_assert (is_valid_layout_index (n));
  return m_layouts [n]->layout;
}

size_t DeepShapeStore::instance_count ()
{
  return s_instance_count;
}

void DeepShapeStore::set_threads (int n)
{
  m_threads = n;
}

void DeepShapeStore::set_max_area_ratio (double ar)
{
  m_max_area_ratio = ar;
}

void DeepShapeStore::set_max_vertex_count (size_t n)
{
  m_max_vertex_count = n;
}

void DeepShapeStore::add_ref (unsigned int layout, unsigned int layer)
{
  tl::MutexLocker locker (&m_lock);

  tl_assert (layout < (unsigned int) m_layouts.size () && m_layouts[layout] != 0);

  m_layouts[layout]->refs += 1;
  m_layouts[layout]->add_layer_ref (layer);
}

void DeepShapeStore::remove_ref (unsigned int layout, unsigned int layer)
{
  tl::MutexLocker locker (&m_lock);

  tl_assert (layout < (unsigned int) m_layouts.size () && m_layouts[layout] != 0);

  m_layouts[layout]->remove_layer_ref (layer);

  if ((m_layouts[layout]->refs -= 1) <= 0) {
    delete m_layouts[layout];
    m_layouts[layout] = 0;
  }
}

unsigned int
DeepShapeStore::layout_for_iter (const db::RecursiveShapeIterator &si, const db::ICplxTrans &trans)
{
  layout_map_type::iterator l = m_layout_map.find (std::make_pair (si, trans));
  if (l == m_layout_map.end ()) {

    unsigned int layout_index = (unsigned int) m_layouts.size ();

    m_layouts.push_back (new LayoutHolder (trans));

    db::Layout &layout = m_layouts.back ()->layout;
    layout.hier_changed_event.add (this, &DeepShapeStore::invalidate_hier);
    if (si.layout ()) {
      layout.dbu (si.layout ()->dbu () / trans.mag ());
    }

    m_layout_map[std::make_pair (si, trans)] = layout_index;
    return layout_index;

  } else {
    return l->second;
  }
}

DeepLayer DeepShapeStore::create_polygon_layer (const db::RecursiveShapeIterator &si, double max_area_ratio, size_t max_vertex_count, const db::ICplxTrans &trans)
{
  if (max_area_ratio == 0.0) {
    max_area_ratio = m_max_area_ratio;
  }
  if (max_vertex_count == 0) {
    max_vertex_count = m_max_vertex_count;
  }

  unsigned int layout_index = layout_for_iter (si, trans);

  db::Layout &layout = m_layouts[layout_index]->layout;
  db::HierarchyBuilder &builder = m_layouts[layout_index]->builder;

  unsigned int layer_index = layout.insert_layer ();
  builder.set_target_layer (layer_index);

  //  The chain of operators for producing clipped and reduced polygon references
  db::PolygonReferenceHierarchyBuilderShapeReceiver refs (& layout, m_text_enlargement, m_text_property_name);
  db::ReducingHierarchyBuilderShapeReceiver red (&refs, max_area_ratio, max_vertex_count);
  db::ClippingHierarchyBuilderShapeReceiver clip (&red);

  //  Build the working hierarchy from the recursive shape iterator
  try {

    tl::SelfTimer timer (tl::verbosity () >= 41, tl::to_string (tr ("Building working hierarchy")));

    builder.set_shape_receiver (&clip);
    db::RecursiveShapeIterator (si).push (& builder);
    builder.set_shape_receiver (0);

  } catch (...) {
    builder.set_shape_receiver (0);
    throw;
  }

  return DeepLayer (this, layout_index, layer_index);
}

DeepLayer DeepShapeStore::create_edge_layer (const db::RecursiveShapeIterator &si, bool as_edges, const db::ICplxTrans &trans)
{
  unsigned int layout_index = layout_for_iter (si, trans);

  db::Layout &layout = m_layouts[layout_index]->layout;
  db::HierarchyBuilder &builder = m_layouts[layout_index]->builder;

  unsigned int layer_index = layout.insert_layer ();
  builder.set_target_layer (layer_index);

  //  The chain of operators for producing edges
  db::EdgeBuildingHierarchyBuilderShapeReceiver refs (as_edges);

  //  Build the working hierarchy from the recursive shape iterator
  try {

    tl::SelfTimer timer (tl::verbosity () >= 41, tl::to_string (tr ("Building working hierarchy")));

    builder.set_shape_receiver (&refs);
    db::RecursiveShapeIterator (si).push (& builder);
    builder.set_shape_receiver (0);

  } catch (...) {
    builder.set_shape_receiver (0);
    throw;
  }

  return DeepLayer (this, layout_index, layer_index);
}

DeepLayer DeepShapeStore::create_edge_pair_layer (const db::RecursiveShapeIterator &si, const db::ICplxTrans &trans)
{
  unsigned int layout_index = layout_for_iter (si, trans);

  db::Layout &layout = m_layouts[layout_index]->layout;
  db::HierarchyBuilder &builder = m_layouts[layout_index]->builder;

  unsigned int layer_index = layout.insert_layer ();
  builder.set_target_layer (layer_index);

  //  The chain of operators for producing the edge pairs
  db::EdgePairBuildingHierarchyBuilderShapeReceiver refs;

  //  Build the working hierarchy from the recursive shape iterator
  try {

    tl::SelfTimer timer (tl::verbosity () >= 41, tl::to_string (tr ("Building working hierarchy")));

    builder.set_shape_receiver (&refs);
    db::RecursiveShapeIterator (si).push (& builder);
    builder.set_shape_receiver (0);

  } catch (...) {
    builder.set_shape_receiver (0);
    throw;
  }

  return DeepLayer (this, layout_index, layer_index);
}

void
DeepShapeStore::invalidate_hier ()
{
  m_delivery_mapping_cache.clear ();
}

void
DeepShapeStore::issue_variants (unsigned int layout_index, const std::map<db::cell_index_type, std::map<db::ICplxTrans, db::cell_index_type> > &var_map)
{
  invalidate_hier ();

  db::HierarchyBuilder &builder = m_layouts [layout_index]->builder;
  for (std::map<db::cell_index_type, std::map<db::ICplxTrans, db::cell_index_type> >::const_iterator i = var_map.begin (); i != var_map.end (); ++i) {
    for (std::map<db::ICplxTrans, db::cell_index_type>::const_iterator j = i->second.begin (); j != i->second.end (); ++j) {
      builder.register_variant (i->first, j->second);
    }
  }
}

const db::CellMapping &
DeepShapeStore::cell_mapping_to_original (unsigned int layout_index, db::Layout *into_layout, db::cell_index_type into_cell, const std::set<db::cell_index_type> *excluded_cells)
{
  const db::Layout *source_layout = &m_layouts [layout_index]->layout;
  if (source_layout->begin_top_down () == source_layout->end_top_cells ()) {
    //  empty source - nothing to do.
    static db::CellMapping cm;
    return cm;
  }

  db::cell_index_type source_top = *source_layout->begin_top_down();

  db::HierarchyBuilder &original_builder = m_layouts [layout_index]->builder;

  //  Derive a cell mapping for source to target. We reuse any existing mapping for returning the
  //  shapes into the original layout.

  DeliveryMappingCacheKey key (layout_index, tl::id_of (into_layout), into_cell);

  std::map<DeliveryMappingCacheKey, db::CellMapping>::iterator cm = m_delivery_mapping_cache.find (key);
  if (cm == m_delivery_mapping_cache.end ()) {

    cm = m_delivery_mapping_cache.insert (std::make_pair (key, db::CellMapping ())).first;

    if (into_layout == original_builder.source ().layout () && &into_layout->cell (into_cell) == original_builder.source ().top_cell ()) {

      //  This is the case of mapping back to the original. In this case we can use the information
      //  provided inside the original hierarchy builders. They list the source cells and the target cells
      //  create from them. We need to consider however, that the hierarchy builder is allowed to create
      //  variants which we cannot map.

      for (HierarchyBuilder::cell_map_type::const_iterator m = original_builder.begin_cell_map (); m != original_builder.end_cell_map (); ++m) {

        HierarchyBuilder::cell_map_type::const_iterator mm = m;
        ++mm;
        bool skip = original_builder.is_variant (m->second);   //  skip variant cells
        while (mm != original_builder.end_cell_map () && mm->first.first == m->first.first && ! skip) {
          //  we have cell variants and cannot simply map
          ++mm;
          ++m;
          skip = true;
        }

        if (! skip) {
          cm->second.map (m->second, m->first.first);
        }

      }

    } else if (into_layout->cells () == 1) {

      //  Another simple case is mapping into an empty (or single-top-cell-only) layout, where we can use "create_from_single_full".
      cm->second.create_single_mapping (*into_layout, into_cell, *source_layout, source_top);

    } else {

      cm->second.create_from_geometry (*into_layout, into_cell, *source_layout, source_top);

    }

    //  Add new cells for the variants and (possible) devices which are cells added during the device
    //  extraction process
    cm->second.create_missing_mapping (*into_layout, into_cell, *source_layout, source_top, excluded_cells);

  }

  return cm->second;
}

void
DeepShapeStore::insert (const DeepLayer &deep_layer, db::Layout *into_layout, db::cell_index_type into_cell, unsigned int into_layer)
{
  db::LayoutLocker locker (into_layout);

  const db::Layout &source_layout = deep_layer.layout ();
  if (source_layout.begin_top_down () == source_layout.end_top_cells ()) {
    //  empty source - nothing to do.
    return;
  }

  //  prepare the transformation
  db::ICplxTrans trans (source_layout.dbu () / into_layout->dbu ());

  //  prepare a layer map
  std::map<unsigned int, unsigned int> lm;
  lm.insert (std::make_pair (deep_layer.layer (), into_layer));

  //  prepare a cell mapping
  const db::CellMapping &cm = cell_mapping_to_original (deep_layer.layout_index (), into_layout, into_cell);

  //  prepare a vector with the source cells
  std::vector <db::cell_index_type> source_cells;
  source_cells.push_back (*source_layout.begin_top_down());

  //  actually copy the shapes
  db::copy_shapes (*into_layout, source_layout, trans, source_cells, cm.table (), lm);
}

void
DeepShapeStore::insert_as_polygons (const DeepLayer &deep_layer, db::Layout *into_layout, db::cell_index_type into_cell, unsigned int into_layer, db::Coord enl)
{
  //  prepare a temporary layer with the polygons
  DeepLayer tmp = deep_layer.derived ();

  db::Layout &layout = const_cast<db::Layout &> (deep_layer.layout ());

  for (db::Layout::iterator c = layout.begin (); c != layout.end (); ++c) {

    db::Shapes &out = c->shapes (tmp.layer ());
    for (db::Shapes::shape_iterator s = c->shapes (deep_layer.layer ()); ! s.at_end (); ++s) {

      if (s->is_edge_pair ()) {

        out.insert (s->edge_pair ().to_simple_polygon (enl));

      } else if (s->is_path () || s->is_polygon () || s->is_box ()) {

        db::Polygon poly;
        s->polygon (poly);
        out.insert (poly);

      }

    }

  }

  //  and insert this one
  insert (tmp, into_layout, into_cell, into_layer);
}

}

