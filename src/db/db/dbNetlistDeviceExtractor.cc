
/*

  KLayout Layout Viewer
  Copyright (C) 2006-2018 Matthias Koefferlein

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

#include "dbNetlistDeviceExtractor.h"
#include "dbNetlistProperty.h"
#include "dbRegion.h"
#include "dbHierNetworkProcessor.h"
#include "dbDeepRegion.h"

namespace db
{

NetlistDeviceExtractor::NetlistDeviceExtractor ()
  : mp_layout (0), m_cell_index (0), mp_circuit (0)
{
  m_device_name_index = 0;
  m_propname_id = 0;
}

NetlistDeviceExtractor::~NetlistDeviceExtractor ()
{
  //  .. nothing yet ..
}

void NetlistDeviceExtractor::initialize (db::Netlist *nl)
{
  m_device_classes.clear ();
  m_device_name_index = 0;
  m_propname_id = 0;
  m_netlist.reset (nl);

  create_device_classes ();
}

static void insert_into_region (const db::PolygonRef &s, const db::ICplxTrans &tr, db::Region &region)
{
  region.insert (s.obj ().transformed (tr * db::ICplxTrans (s.trans ())));
}

void NetlistDeviceExtractor::extract (const std::vector<db::Region *> regions)
{
  tl_assert (! regions.empty ());

  const db::Layout *layout = 0;
  const db::Cell *cell = 0;
  std::vector<unsigned int> layers;
  layers.reserve (regions.size ());

  for (std::vector<db::Region *>::const_iterator r = regions.begin (); r != regions.end (); ++r) {

    //  TODO: this is clumsy ...
    db::DeepRegion *dr = dynamic_cast<db::DeepRegion *> ((*r)->delegate ());
    tl_assert (dr != 0);

    db::DeepLayer dl = dr->deep_layer ();
    tl_assert (dl.layout () != 0);
    tl_assert (dl.initial_cell () != 0);

    if (! layout) {
      layout = dl.layout ();
    } else {
      tl_assert (layout == dl.layout ());
    }

    if (! cell) {
      cell = dl.initial_cell ();
    } else {
      tl_assert (cell == dl.initial_cell ());
    }

    layers.push_back (dl.layer ());

  }

  //  NOTE: the const_cast's are there because the extraction will annotate the layout with port
  //  shapes. That's not part of the initial design, where the underlying deep shape store is
  //  immutable from the outside. But we know what we're doing.
  extract (const_cast<db::Layout &> (*layout), const_cast<db::Cell &> (*cell), layers);
}

void NetlistDeviceExtractor::extract (db::Layout &layout, db::Cell &cell, const std::vector<unsigned int> &layers)
{
  typedef db::PolygonRef shape_type;
  db::ShapeIterator::flags_type shape_iter_flags = db::ShapeIterator::Polygons;

  mp_layout = &layout;
  m_layers = layers;

  //  terminal properties are kept in property index 0
  m_propname_id = mp_layout->properties_repository ().prop_name_id (tl::Variant (int (0)));

  tl_assert (m_netlist.get () != 0);

  //  build a cell-id-to-circuit lookup table
  std::map<db::cell_index_type, db::Circuit *> circuits_by_cell;
  for (db::Netlist::circuit_iterator c = m_netlist->begin_circuits (); c != m_netlist->end_circuits (); ++c) {
    circuits_by_cell.insert (std::make_pair (c->cell_index (), c.operator-> ()));
  }

  //  collect the cells below the top cell
  std::set<db::cell_index_type> called_cells;
  called_cells.insert (cell.cell_index ());
  cell.collect_called_cells (called_cells);

  //  build the device clusters
  db::Connectivity device_conn = get_connectivity (layout, layers);
  db::hier_clusters<shape_type> device_clusters;
  device_clusters.build (layout, cell, shape_iter_flags, device_conn);

  //  for each cell investigate the clusters
  for (std::set<db::cell_index_type>::const_iterator ci = called_cells.begin (); ci != called_cells.end (); ++ci) {

    m_cell_index = *ci;

    std::map<db::cell_index_type, db::Circuit *>::const_iterator c2c = circuits_by_cell.find (*ci);
    if (c2c != circuits_by_cell.end ()) {

      //  reuse existing circuit
      mp_circuit = c2c->second;

    } else {

      //  create a new circuit for this cell
      mp_circuit = new db::Circuit ();
      mp_circuit->set_cell_index (*ci);
      mp_circuit->set_name (layout.cell_name (*ci));
      m_netlist->add_circuit (mp_circuit);

    }

    //  investigate each cluster
    db::connected_clusters<shape_type> cc = device_clusters.clusters_per_cell (*ci);
    for (db::connected_clusters<shape_type>::all_iterator c = cc.begin_all (); !c.at_end(); ++c) {

      //  take only root clusters - others have upward connections and are not "whole"
      if (! cc.is_root (*c)) {
        continue;
      }

      //  build layer geometry from the cluster found

      std::vector<db::Region> layer_geometry;
      layer_geometry.resize (layers.size ());

      for (std::vector<unsigned int>::const_iterator l = layers.begin (); l != layers.end (); ++l) {
        db::Region &r = layer_geometry [l - layers.begin ()];
        for (db::recursive_cluster_shape_iterator<shape_type> si (device_clusters, *l, *ci, *c); ! si.at_end(); ++si) {
          insert_into_region (*si, si.trans (), r);
        }
      }

      //  do the actual device extraction
      extract_devices (layer_geometry);

    }

  }
}

void NetlistDeviceExtractor::create_device_classes ()
{
  //  .. the default implementation does nothing ..
}

db::Connectivity NetlistDeviceExtractor::get_connectivity (const db::Layout & /*layout*/, const std::vector<unsigned int> & /*layers*/) const
{
  //  .. the default implementation does nothing ..
  return db::Connectivity ();
}

void NetlistDeviceExtractor::extract_devices (const std::vector<db::Region> & /*layer_geometry*/)
{
  //  .. the default implementation does nothing ..
}

void NetlistDeviceExtractor::register_device_class (DeviceClass *device_class)
{
  tl_assert (m_netlist.get () != 0);
  m_netlist->add_device_class (device_class);
  m_device_classes.push_back (device_class);
}

Device *NetlistDeviceExtractor::create_device (unsigned int device_class_index)
{
  tl_assert (mp_circuit != 0);
  tl_assert (device_class_index < m_device_classes.size ());
  Device *device = new Device (m_device_classes[device_class_index], tl::to_string (++m_device_name_index));
  mp_circuit->add_device (device);
  return device;
}

void NetlistDeviceExtractor::define_terminal (Device *device, size_t terminal_id, size_t geometry_index, const db::Polygon &polygon)
{
  tl_assert (mp_layout != 0);
  tl_assert (geometry_index < m_layers.size ());
  unsigned int layer_index = m_layers [geometry_index];

  //  Build a property set for the DeviceTerminalProperty
  db::PropertiesRepository::properties_set ps;
  tl::Variant &v = ps.insert (std::make_pair (m_propname_id, tl::Variant ()))->second;
  v = tl::Variant (new db::DeviceTerminalProperty (device->id (), terminal_id), db::NetlistProperty::variant_class (), true);
  db::properties_id_type pi = mp_layout->properties_repository ().properties_id (ps);

  db::PolygonRef pr (polygon, mp_layout->shape_repository ());
  mp_layout->cell (m_cell_index).shapes (layer_index).insert (db::PolygonRefWithProperties (pr, pi));
}

void NetlistDeviceExtractor::define_terminal (Device *device, size_t terminal_id, size_t layer_index, const db::Box &box)
{
  define_terminal (device, terminal_id, layer_index, db::Polygon (box));
}

void NetlistDeviceExtractor::define_terminal (Device *device, size_t terminal_id, size_t layer_index, const db::Point &point)
{
  //  NOTE: we add one DBU to the "point" to prevent it from vanishing
  db::Vector dv (1, 1);
  define_terminal (device, terminal_id, layer_index, db::Polygon (db::Box (point - dv, point + dv)));
}

}
