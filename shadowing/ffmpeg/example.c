#include <libavutil/avutil.h>
#include <libavutil/executor.h>

unsigned int ffmpeg_avutil_version(void) {
  return avutil_version();
}
