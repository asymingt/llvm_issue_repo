#include <new>
struct Foo {
  int x;
  __device__ explicit Foo(int v) : x(v) {}
};
__global__ void kernel(int* out) {
  alignas(Foo) unsigned char buf[sizeof(Foo)];
  Foo* f = new (buf) Foo(42);
  *out = f->x;
}