#version 310 es
precision mediump float;

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
void f() {
  vec3 a = vec3(1.0f, 2.0f, 3.0f);
  vec3 b = vec3(4.0f, 5.0f, 6.0f);
  vec3 r = (a + b);
  return;
}
void main() {
  f();
}

