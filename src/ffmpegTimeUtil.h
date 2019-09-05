#pragma once

#include <chrono>
extern "C"
{
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
#include <libavutil/rational.h>
}

namespace ffmpeg
{

typedef std::chrono::duration<int64_t, std::ratio<1, AV_TIME_BASE>>
    ffmpeg_duration_t;

template <typename Chrono_t>
Chrono_t get_timestamp(uint64_t ts, const AVRational &tb)
{
  if constexpr (std::is_floating_point_v<typename Chrono_t::rep>)
  {
    std::chrono::duration<double> T(av_q2d(tb) * ts);
    return std::chrono::duration_cast<Chrono_t>(T);
  }
  else
  {
    return Chrono_t(
        av_rescale_q(ts, tb, {Chrono_t::period::num, Chrono_t::period::den}));
  }
}

template <typename Chrono_t> int64_t get_pts(Chrono_t t, const AVRational &tb)
{
  if constexpr (std::is_floating_point_v<typename Chrono_t::rep>)
  {
    using dbl_duration_t = std::chrono::duration<double>;
    dbl_duration_t T = std::chrono::duration_cast<dbl_duration_t>(t);
    return (int64_t)std::round(T.count() / av_q2d(tb));
  }
  else
  {
    return av_rescale_q(t.count(),
                        {Chrono_t::period::num, Chrono_t::period::den}, tb);
  }
}

} // namespace ffmpeg
