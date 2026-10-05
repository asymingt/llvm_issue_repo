#include "osqp.h"
#include "error.h"

// _osqp_error is declared in @osqp's include/error.h, which is shadowed by the
// toolchain's glibc <error.h> without header_search_paths.patch.
c_int trigger_osqp_error(void) {
  return osqp_error(OSQP_DATA_VALIDATION_ERROR);
}
