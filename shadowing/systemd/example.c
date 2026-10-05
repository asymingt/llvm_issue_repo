#include <libudev.h>

struct udev *create_udev_context(void) {
  return udev_new();
}
