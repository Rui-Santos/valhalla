#include "midgard/logging.h"
#include "midgard/encoded.h"
#include "loki/worker.h"
#include "tyr/serializers.h"

using namespace valhalla;
using namespace valhalla::tyr;
using namespace valhalla::midgard;
using namespace valhalla::baldr;
using namespace valhalla::skadi;

namespace {
PointLL to_ll(const odin::Location& l) {
  return PointLL{l.ll().lng(), l.ll().lat()};
}
void from_ll(odin::Location* l, const PointLL& p) {
  l->mutable_ll()->set_lat(p.lat());
  l->mutable_ll()->set_lng(p.lng());
}
}

namespace valhalla {
  namespace loki {

    std::vector<PointLL> loki_worker_t::init_height(valhalla_request_t& request) {
      //not enough shape
      if (request.options.shape_size() < 1)
        throw valhalla_exception_t{312};

      //convert back to native pointll :(
      std::vector<PointLL> shape;
      for(const auto& l : request.options.shape()) shape.emplace_back(to_ll(l));

      //resample the shape
      bool resampled = false;
      if(request.options.has_resample_distance()) {
        if(request.options.resample_distance() < min_resample)
          throw valhalla_exception_t{313, " " + std::to_string(min_resample) + " meters"};
        if(request.options.shape_size() > 1) {
          //resample the shape but make sure to keep the first and last shapepoint
          auto last = shape.back();
          shape = midgard::resample_spherical_polyline(shape, request.options.resample_distance());
          shape.emplace_back(std::move(last));
          //put it back
          request.options.clear_shape();
          for(const auto& p : shape) from_ll(request.options.mutable_shape()->Add(), p);
          //reencode it for display if they sent it encoded
          if(request.options.has_encoded_polyline())
            request.options.set_encoded_polyline(midgard::encode(shape));
          resampled = true;
        }
      }

      //there are limits though
      if(request.options.shape_size() > max_elevation_shape) {
        throw valhalla_exception_t{314, " (" + std::to_string(request.options.shape_size()) +
            (resampled ? " after resampling" : "") + "). The limit is " + std::to_string(max_elevation_shape)};
      }

      return shape;
    }

    /* example height with range response:
    {
      "shape": [ {"lat": 40.712433, "lon": -76.504913}, {"lat": 40.712276, "lon": -76.605263} ],
      "range_height": [ [0,303], [8467,275], [25380,198] ]
    }
    */
    std::string loki_worker_t::height(valhalla_request_t& request) {
      auto shape = init_height(request);
      //get the elevation of each posting
      std::vector<double> heights = sample.get_all(shape);
      if (!request.options.do_not_track())
        valhalla::midgard::logging::Log("sample_count::" + std::to_string(shape.size()), " [ANALYTICS] ");

      //get the distances between the postings if desired
      std::vector<float> ranges;
      if (request.options.range()) {
        ranges.reserve(shape.size()); ranges.emplace_back(0);
        for(auto point = std::next(shape.cbegin()); point != shape.cend(); ++point)
          ranges.emplace_back(ranges.back() +  point->Distance(*std::prev(point)));
      }

      return tyr::serializeHeight(request, heights, ranges);
    }
  }
}
