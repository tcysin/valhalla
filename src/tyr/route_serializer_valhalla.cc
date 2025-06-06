#include "baldr/rapidjson_utils.h"
#include "midgard/aabb2.h"
#include "midgard/logging.h"
#include "odin/enhancedtrippath.h"
#include "odin/util.h"
#include "proto_conversions.h"
#include "tyr/serializers.h"

#include <vector>

using namespace valhalla;
using namespace valhalla::midgard;
using namespace valhalla::odin;
using namespace valhalla::baldr;

namespace {

namespace valhalla_serializers {
/*
valhalla output looks like this:
{
    "trip":
{
    "status": 0,
    "locations": [
       {
        "longitude": -76.4791,
        "latitude": 40.4136,
         "stopType": 0
       },
       {
        "longitude": -76.5352,
        "latitude": 40.4029,
        "stopType": 0
       }
     ],
    "units": "kilometers"
    "summary":
{
    "distance": 4973,
    "time": 325,
    "cost": 304
},
"legs":
[
  {
      "summary":
  {
      "distance": 4973,
      "time": 325,
      "cost": 304
  },
  "maneuvers":
  [
    {
        "beginShapeIndex": 0,
        "distance": 633,
        "writtenInstruction": "Start out going west on West Market Street.",
        "streetNames":
        [
            "West Market Street"
        ],
        "type": 1,
        "time": 41,
        "cost": 23
    },
    {
        "beginShapeIndex": 7,
        "distance": 4340,
        "writtenInstruction": "Continue onto Jonestown Road.",
        "streetNames":
        [
            "Jonestown Road"
        ],
        "type": 8,
        "time": 284,
        "cost": 281
    },
    {
        "beginShapeIndex": 40,
        "distance": 0,
        "writtenInstruction": "You have arrived at your destination.",
        "type": 4,
        "time": 0,
        "cost": 0
    }
],
"shape":
"gysalAlg|zpC~Clt@tDtx@hHfaBdKl{BrKbnApGro@tJrz@jBbQj@zVt@lTjFnnCrBz}BmFnoB]pHwCvm@eJxtATvXTnfAk@|^z@rGxGre@nTpnBhBbQvXduCrUr`Edd@naEja@~gAhk@nzBxf@byAfm@tuCvDtOvNzi@|jCvkKngAl`HlI|}@`N`{Adx@pjE??xB|J"
}
],
"status_message": "Found route between points"
},
"id": "work route"
}
*/

void summary(const valhalla::Api& api, int route_index, rapidjson::writer_wrapper_t& writer) {
  double route_time = 0;
  double route_length = 0;
  double route_cost = 0;
  bool has_time_restrictions = false;
  bool has_toll = false;
  bool has_highway = false;
  bool has_ferry = false;
  AABB2<PointLL> bbox(10000.0f, 10000.0f, -10000.0f, -10000.0f);
  std::vector<double> recost_times(api.options().recostings_size(), 0);
  for (int leg_index = 0; leg_index < api.directions().routes(route_index).legs_size(); ++leg_index) {
    const auto& leg = api.directions().routes(route_index).legs(leg_index);
    const auto& trip_leg = api.trip().routes(route_index).legs(leg_index);
    route_time += leg.summary().time();
    route_length += leg.summary().length();
    route_cost += trip_leg.node().rbegin()->cost().elapsed_cost().cost();

    // recostings
    const auto& recosts = trip_leg.node().rbegin()->recosts();
    auto recost_time_itr = recost_times.begin();
    for (const auto& recost : recosts) {
      if (!recost.has_elapsed_cost() || (*recost_time_itr) < 0)
        (*recost_time_itr) = -1;
      else
        (*recost_time_itr) += recost.elapsed_cost().seconds();
      ++recost_time_itr;
    }

    AABB2<PointLL> leg_bbox(leg.summary().bbox().min_ll().lng(), leg.summary().bbox().min_ll().lat(),
                            leg.summary().bbox().max_ll().lng(), leg.summary().bbox().max_ll().lat());
    bbox.Expand(leg_bbox);
    has_time_restrictions = has_time_restrictions || leg.summary().has_time_restrictions();
    has_toll = has_toll || leg.summary().has_toll();
    has_highway = has_highway || leg.summary().has_highway();
    has_ferry = has_ferry || leg.summary().has_ferry();
  }

  writer.start_object("summary");
  writer("has_time_restrictions", has_time_restrictions);
  writer("has_toll", has_toll);
  writer("has_highway", has_highway);
  writer("has_ferry", has_ferry);
  writer.set_precision(tyr::kCoordinatePrecision);
  writer("min_lat", bbox.miny());
  writer("min_lon", bbox.minx());
  writer("max_lat", bbox.maxy());
  writer("max_lon", bbox.maxx());
  writer.set_precision(tyr::kDefaultPrecision);
  writer("time", route_time);
  writer.set_precision(api.options().units() == Options::miles ? 4 : 3);
  writer("length", route_length);
  writer.set_precision(tyr::kDefaultPrecision);
  writer("cost", route_cost);
  auto recost_itr = api.options().recostings().begin();
  for (auto recost : recost_times) {
    if (recost < 0)
      writer("time_" + recost_itr->name(), std::nullptr_t());
    else
      writer("time_" + recost_itr->name(), recost);
    ++recost_itr;
  }
  writer.end_object();

  writer("status_message", "Found route between points");
  writer("status", 0); // 0 success
  writer("units", valhalla::Options_Units_Enum_Name(api.options().units()));
  writer("language", api.options().language());

  LOG_DEBUG("trip_time::" + std::to_string(route_time) + "s");
}

void locations(const valhalla::Api& api, int route_index, rapidjson::writer_wrapper_t& writer) {

  int index = 0;
  writer.set_precision(tyr::kCoordinatePrecision);
  writer.start_array("locations");
  for (const auto& leg : api.directions().routes(route_index).legs()) {
    for (auto location = leg.location().begin() + index; location != leg.location().end();
         ++location) {
      index = 1;
      writer.start_object();

      writer("type", Location_Type_Enum_Name(location->type()));
      writer("lat", location->ll().lat());
      writer("lon", location->ll().lng());
      if (!location->name().empty()) {
        writer("name", location->name());
      }

      if (!location->street().empty()) {
        writer("street", location->street());
      }

      if (location->has_heading_case()) {
        writer("heading", static_cast<uint64_t>(location->heading()));
      }

      if (!location->date_time().empty()) {
        writer("date_time", location->date_time());
      }

      if (!location->time_zone_offset().empty()) {
        writer("time_zone_offset", location->time_zone_offset());
      }

      if (!location->time_zone_name().empty()) {
        writer("time_zone_name", location->time_zone_name());
      }

      if (location->waiting_secs()) {
        writer("waiting", static_cast<uint64_t>(location->waiting_secs()));
      }

      if (location->side_of_street() != valhalla::Location::kNone) {
        writer("side_of_street", Location_SideOfStreet_Enum_Name(location->side_of_street()));
      }

      writer("original_index", location->correlation().original_index());

      writer.end_object();
    }
  }

  writer.end_array();
}

// Serialize turn lane information
void turn_lanes(const TripLeg& leg,
                const DirectionsLeg_Maneuver& maneuver,
                rapidjson::writer_wrapper_t& writer) {

  // Read edge from a trip leg
  if (maneuver.begin_path_index() == 0 || maneuver.begin_path_index() >= leg.node_size())
    return;

  auto prev_index = maneuver.begin_path_index() - 1;
  const auto& prev_edge = leg.node(prev_index).edge();

  if (prev_edge.turn_lanes_size() > 1) {
    writer.start_array("lanes");

    for (const auto& turn_lane : prev_edge.turn_lanes()) {
      writer.start_object();

      // Directions as a bit mask
      writer("directions", turn_lane.directions_mask());

      if (turn_lane.state() == TurnLane::kActive) {
        writer("active", turn_lane.active_direction());
      } else if (turn_lane.state() == TurnLane::kValid) {
        writer("valid", turn_lane.active_direction());
      }

      writer.end_object();
    }

    writer.end_array();
  }
}

void legs(valhalla::Api& api, int route_index, rapidjson::writer_wrapper_t& writer) {
  writer.start_array("legs");
  const auto& directions_legs = api.directions().routes(route_index).legs();
  unsigned int length_prec = api.options().units() == Options::miles ? 4 : 3;
  auto trip_leg_itr = api.mutable_trip()->mutable_routes(route_index)->mutable_legs()->begin();
  for (const auto& directions_leg : directions_legs) {
    valhalla::odin::EnhancedTripLeg etp(*trip_leg_itr);
    writer.start_object(); // leg
    bool has_time_restrictions = false;
    bool has_toll = false;
    bool has_highway = false;
    bool has_ferry = false;

    if (directions_leg.maneuver_size())
      writer.start_array("maneuvers");

    int maneuver_index = 0;
    for (const auto& maneuver : directions_leg.maneuver()) {
      writer.start_object();

      // Maneuver type
      writer("type", static_cast<uint64_t>(maneuver.type()));

      // Instruction and verbal instructions
      writer("instruction", maneuver.text_instruction());
      if (!maneuver.verbal_transition_alert_instruction().empty()) {
        writer("verbal_transition_alert_instruction", maneuver.verbal_transition_alert_instruction());
      }
      if (!maneuver.verbal_succinct_transition_instruction().empty()) {
        writer("verbal_succinct_transition_instruction",
               maneuver.verbal_succinct_transition_instruction());
      }
      if (!maneuver.verbal_pre_transition_instruction().empty()) {
        writer("verbal_pre_transition_instruction", maneuver.verbal_pre_transition_instruction());
      }
      if (!maneuver.verbal_post_transition_instruction().empty()) {
        writer("verbal_post_transition_instruction", maneuver.verbal_post_transition_instruction());
      }

      // Set street names
      if (maneuver.street_name_size() > 0) {
        writer.start_array("street_names");
        for (int i = 0; i < maneuver.street_name_size(); i++) {
          writer(maneuver.street_name(i).value());
        }
        writer.end_array();
      }

      // Set begin street names
      if (maneuver.begin_street_name_size() > 0) {
        writer.start_array("begin_street_names");
        for (int i = 0; i < maneuver.begin_street_name_size(); i++) {
          writer(maneuver.begin_street_name(i).value());
        }
        writer.end_array();
      }

      // Set bearings
      // absolute bearing (degrees from north, clockwise) before and after the maneuver.
      bool depart_maneuver = (maneuver_index == 0);
      bool arrive_maneuver = (maneuver_index == directions_leg.maneuver_size() - 1);
      if (!depart_maneuver) {
        uint32_t node_index = maneuver.begin_path_index();
        uint32_t in_brg = etp.GetPrevEdge(node_index)->end_heading();
        writer("bearing_before", in_brg);
      }
      if (!arrive_maneuver) {
        uint32_t out_brg = maneuver.begin_heading();
        writer("bearing_after", out_brg);
      }

      // Time, length, cost, and shape indexes
      const auto& end_node = trip_leg_itr->node(maneuver.end_path_index());
      const auto& begin_node = trip_leg_itr->node(maneuver.begin_path_index());
      auto cost = end_node.cost().elapsed_cost().cost() - begin_node.cost().elapsed_cost().cost();

      writer.set_precision(tyr::kDefaultPrecision);
      writer("time", maneuver.time());
      writer.set_precision(length_prec);
      writer("length", maneuver.length());
      writer.set_precision(tyr::kDefaultPrecision);
      writer("cost", cost);
      writer("begin_shape_index", maneuver.begin_shape_index());
      writer("end_shape_index", maneuver.end_shape_index());
      auto recost_itr = api.options().recostings().begin();
      auto begin_recost_itr = begin_node.recosts().begin();
      for (const auto& end_recost : end_node.recosts()) {
        if (end_recost.has_elapsed_cost())
          writer("time_" + recost_itr->name(),
                 end_recost.elapsed_cost().seconds() - begin_recost_itr->elapsed_cost().seconds());
        else
          writer("time_" + recost_itr->name(), std::nullptr_t());
        ++recost_itr;
      }

      // Portions toll, highway, ferry and rough
      if (maneuver.portions_toll()) {
        writer("toll", maneuver.portions_toll());
        has_toll = true;
      }
      if (maneuver.portions_highway()) {
        writer("highway", maneuver.portions_highway());
        has_highway = true;
      }
      if (maneuver.portions_ferry()) {
        writer("ferry", maneuver.portions_ferry());
        has_ferry = true;
      }
      if (maneuver.portions_unpaved()) {
        writer("rough", maneuver.portions_unpaved());
      }
      if (maneuver.has_time_restrictions()) {
        writer("has_time_restrictions", maneuver.has_time_restrictions());
        has_time_restrictions = true;
      }

      // Process sign
      if (maneuver.has_sign()) {
        writer.start_object("sign");

        // Process exit number
        if (maneuver.sign().exit_numbers_size() > 0) {
          writer.start_array("exit_number_elements");
          for (int i = 0; i < maneuver.sign().exit_numbers_size(); ++i) {
            writer.start_object();
            // Add the exit number text
            writer("text", maneuver.sign().exit_numbers(i).text());
            // Add the exit number consecutive count only if greater than zero
            if (maneuver.sign().exit_numbers(i).consecutive_count() > 0) {
              writer("consecutive_count", maneuver.sign().exit_numbers(i).consecutive_count());
            }
            writer.end_object();
          }
          writer.end_array();
        }

        // Process exit branch
        if (maneuver.sign().exit_onto_streets_size() > 0) {
          writer.start_array("exit_branch_elements");
          for (int i = 0; i < maneuver.sign().exit_onto_streets_size(); ++i) {
            writer.start_object();
            // Add the exit branch text
            writer("text", maneuver.sign().exit_onto_streets(i).text());
            // Add the exit branch consecutive count only if greater than zero
            if (maneuver.sign().exit_onto_streets(i).consecutive_count() > 0) {
              writer("consecutive_count", maneuver.sign().exit_onto_streets(i).consecutive_count());
            }
            writer.end_object();
          }
          writer.end_array();
        }

        // Process exit toward
        if (maneuver.sign().exit_toward_locations_size() > 0) {
          writer.start_array("exit_toward_elements");
          for (int i = 0; i < maneuver.sign().exit_toward_locations_size(); ++i) {
            writer.start_object();
            // Add the exit toward text
            writer("text", maneuver.sign().exit_toward_locations(i).text());
            // Add the exit toward consecutive count only if greater than zero
            if (maneuver.sign().exit_toward_locations(i).consecutive_count() > 0) {
              writer("consecutive_count",
                     maneuver.sign().exit_toward_locations(i).consecutive_count());
            }
            writer.end_object();
          }
          writer.end_array();
        }

        // Process exit name
        if (maneuver.sign().exit_names_size() > 0) {
          writer.start_array("exit_name_elements");
          for (int i = 0; i < maneuver.sign().exit_names_size(); ++i) {
            writer.start_object();
            // Add the exit name text
            writer("text", maneuver.sign().exit_names(i).text());
            // Add the exit name consecutive count only if greater than zero
            if (maneuver.sign().exit_names(i).consecutive_count() > 0) {
              writer("consecutive_count", maneuver.sign().exit_names(i).consecutive_count());
            }
            writer.end_object();
          }
          writer.end_array();
        }

        writer.end_object(); // sign
      }

      // Roundabout count
      if (maneuver.roundabout_exit_count() > 0) {
        writer("roundabout_exit_count", maneuver.roundabout_exit_count());
      }

      // Depart and arrive instructions
      if (!maneuver.depart_instruction().empty()) {
        writer("depart_instruction", maneuver.depart_instruction());
      }
      if (!maneuver.verbal_depart_instruction().empty()) {
        writer("verbal_depart_instruction", maneuver.verbal_depart_instruction());
      }
      if (!maneuver.arrive_instruction().empty()) {
        writer("arrive_instruction", maneuver.arrive_instruction());
      }
      if (!maneuver.verbal_arrive_instruction().empty()) {
        writer("verbal_arrive_instruction", maneuver.verbal_arrive_instruction());
      }

      // Process transit route
      if (maneuver.has_transit_info()) {
        const auto& transit_info = maneuver.transit_info();
        writer.start_object("transit_info");

        if (!transit_info.onestop_id().empty()) {
          writer("onestop_id", transit_info.onestop_id());
        }
        if (!transit_info.short_name().empty()) {
          writer("short_name", transit_info.short_name());
        }
        if (!transit_info.long_name().empty()) {
          writer("long_name", transit_info.long_name());
        }
        if (!transit_info.headsign().empty()) {
          writer("headsign", transit_info.headsign());
        }
        writer("color", transit_info.color());
        writer("text_color", transit_info.text_color());
        if (!transit_info.description().empty()) {
          writer("description", transit_info.description());
        }
        if (!transit_info.operator_onestop_id().empty()) {
          writer("operator_onestop_id", transit_info.operator_onestop_id());
        }
        if (!transit_info.operator_name().empty()) {
          writer("operator_name", transit_info.operator_name());
        }
        if (!transit_info.operator_url().empty()) {
          writer("operator_url", transit_info.operator_url());
        }

        // Add transit stops
        if (transit_info.transit_stops().size() > 0) {
          writer.start_array("transit_stops");
          for (const auto& transit_stop : transit_info.transit_stops()) {
            writer.start_object();

            // type
            if (transit_stop.type() == TransitPlatformInfo_Type_kStation) {
              writer("type", std::string("station"));
            } else {
              writer("type", std::string("stop"));
            }

            // onestop_id - using the station onestop_id
            if (!transit_stop.station_onestop_id().empty()) {
              writer("onestop_id", transit_stop.station_onestop_id());
            }

            // name - using the station name
            if (!transit_stop.station_name().empty()) {
              writer("name", transit_stop.station_name());
            }

            // arrival_date_time
            if (!transit_stop.arrival_date_time().empty()) {
              writer("arrival_date_time", transit_stop.arrival_date_time());
            }

            // departure_date_time
            if (!transit_stop.departure_date_time().empty()) {
              writer("departure_date_time", transit_stop.departure_date_time());
            }

            // assumed_schedule
            writer("assumed_schedule", transit_stop.assumed_schedule());

            // latitude and longitude
            if (transit_stop.has_ll()) {
              writer.set_precision(tyr::kCoordinatePrecision);
              writer("lat", transit_stop.ll().lat());
              writer("lon", transit_stop.ll().lng());
            }

            writer.end_object(); // transit_stop
          }
          writer.end_array(); // transit_stops
        }
        writer.end_object(); // transit_info
      }

      if (maneuver.verbal_multi_cue()) {
        writer("verbal_multi_cue", maneuver.verbal_multi_cue());
      }

      // Travel mode
      auto mode_type = travel_mode_type(maneuver);
      writer("travel_mode", mode_type.first);

      // Travel type
      writer("travel_type", mode_type.second);

      //  man->emplace("hasGate", maneuver.);
      //  man->emplace("hasFerry", maneuver.);
      // “portionsTollNote” : “<portionsTollNote>”,
      // “portionsUnpavedNote” : “<portionsUnpavedNote>”,
      // “gateAccessRequiredNote” : “<gateAccessRequiredNote>”,
      // “checkFerryInfoNote” : “<checkFerryInfoNote>”

      // Add Line info if enabled
      if (api.options().turn_lanes()) {
        turn_lanes(*trip_leg_itr, maneuver, writer);
      }

      writer.end_object(); // maneuver
      maneuver_index++;
    }
    if (directions_leg.maneuver_size()) {
      writer.end_array(); // maneuvers
    }

    // Store elevation for the leg
    if (api.options().elevation_interval() > 0.0f) {
      writer.set_precision(1);
      float unit_factor = api.options().units() == Options::miles ? kFeetPerMeter : 1.0f;
      float interval = api.options().elevation_interval();
      writer("elevation_interval", interval * unit_factor);
      auto elevation = tyr::get_elevation(*trip_leg_itr, interval);

      writer.start_array("elevation");
      for (const auto& h : elevation) {
        writer(h * unit_factor);
      }
      writer.end_array(); // elevation
    }

    writer.start_object("summary");

    // Does the user want admin info?
    if (api.options().admin_crossings()) {
      // write the admin array
      writer.start_array("admins");
      for (const auto& admin : trip_leg_itr->admin()) {
        writer.start_object();
        writer("country_code", admin.country_code());
        writer("country_text", admin.country_text());
        writer("state_code", admin.state_code());
        writer("state_text", admin.state_text());
        writer.end_object();
      }
      writer.end_array();

      if (trip_leg_itr->admin_size() > 1) {
        // write the admin crossings
        auto node_itr = trip_leg_itr->node().begin();
        auto next_node_itr = trip_leg_itr->node().begin();
        next_node_itr++;
        writer.start_array("admin_crossings");

        while (next_node_itr != trip_leg_itr->node().end()) {
          if (next_node_itr->admin_index() != node_itr->admin_index()) {
            writer.start_object();
            writer("from_admin_index", node_itr->admin_index());
            writer("to_admin_index", next_node_itr->admin_index());
            writer("begin_shape_index", node_itr->edge().begin_shape_index());
            writer("end_shape_index", node_itr->edge().end_shape_index());
            writer.end_object();
          }
          ++node_itr;
          ++next_node_itr;
        }
        writer.end_array();
      }
    }

    // are there any level changes along the leg
    if (directions_leg.level_changes().size() > 0) {
      writer.start_array("level_changes");
      for (auto& level_change : directions_leg.level_changes()) {
        writer.start_array();
        writer(level_change.shape_index());
        writer.set_precision(std::max(level_change.precision(), static_cast<uint32_t>(1)));
        writer(level_change.level());
        writer.set_precision(tyr::kDefaultPrecision);
        writer.end_array();
      }
      writer.end_array();
    }

    writer("has_time_restrictions", has_time_restrictions);
    writer("has_toll", has_toll);
    writer("has_highway", has_highway);
    writer("has_ferry", has_ferry);
    writer.set_precision(tyr::kCoordinatePrecision);
    writer("min_lat", directions_leg.summary().bbox().min_ll().lat());
    writer("min_lon", directions_leg.summary().bbox().min_ll().lng());
    writer("max_lat", directions_leg.summary().bbox().max_ll().lat());
    writer("max_lon", directions_leg.summary().bbox().max_ll().lng());
    writer.set_precision(tyr::kDefaultPrecision);
    writer("time", directions_leg.summary().time());
    writer.set_precision(length_prec);
    writer("length", directions_leg.summary().length());
    writer.set_precision(tyr::kDefaultPrecision);
    writer("cost", trip_leg_itr->node().rbegin()->cost().elapsed_cost().cost());
    auto recost_itr = api.options().recostings().begin();
    for (const auto& recost : trip_leg_itr->node().rbegin()->recosts()) {
      if (recost.has_elapsed_cost())
        writer("time_" + recost_itr->name(), recost.elapsed_cost().seconds());
      else
        writer("time_" + recost_itr->name(), std::nullptr_t());
      ++recost_itr;
    }
    ++trip_leg_itr;
    writer.end_object();

    writer("shape", directions_leg.shape());

    writer.end_object(); // leg
  }
  writer.end_array(); // legs
}

std::string serialize(Api& api) {
  // build up the json object, reserve 4k bytes
  rapidjson::writer_wrapper_t writer(4096);

  // for each route
  for (int i = 0; i < api.directions().routes_size(); ++i) {
    if (i == 1) {
      writer.start_array("alternates");
    }

    // the route itself
    writer.start_object();
    writer.start_object("trip");

    // the locations in the trip
    locations(api, i, writer);

    // the actual meat of the route
    legs(api, i, writer);

    // openlr references of the edges in the route
    valhalla::tyr::openlr(api, i, writer);

    // summary time/distance and other stats
    summary(api, i, writer);

    // get serialized warnings
    if (api.info().warnings_size() >= 1) {
      valhalla::tyr::serializeWarnings(api, writer);
    }

    writer.end_object(); // trip

    // leave space for alternates by closing this one outside the loop
    if (i > 0) {
      writer.end_object();
    }
  }

  if (api.directions().routes_size() > 1) {
    writer.end_array(); // alternates
  }

  if (api.options().has_id_case()) {
    writer("id", api.options().id());
  }

  writer.end_object(); // outer object

  return writer.get_buffer();
}
} // namespace valhalla_serializers
} // namespace
