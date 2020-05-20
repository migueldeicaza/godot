#ifndef GDSWIFTMARSHAL_C_API_H
#define GDSWIFTMARSHAL_C_API_H
#include <stdint.h>

// This is broken, need to include the proper header files,
// but swift chokes on C++ header files
typedef float real_t;
#pragma pack(push, 1)

typedef struct {
	real_t x, y;
} Vector2;

typedef struct {
	Vector2 position;
	Vector2 size;
} Rect2;

typedef struct {
	Vector2 elements[3];
} Transform2D;

typedef struct {
	real_t x, y, z;
} Vector3;

typedef struct  {
	Vector3 elements[3];
} Basis;

typedef struct {
	real_t x, y, z, w;
} Quat;

typedef struct  {
	Basis basis;
	Vector3 origin;
}Transform;

typedef struct  {
	Vector3 position;
	Vector3 size;
}AABB;

typedef struct  {
	float r, g, b, a;
}Color;

typedef struct  {
	Vector3 normal;
	real_t d;
} Plane;

#define MAKE_STRUCT_ARRAY(n) typedef struct { int count; const n *data; } n ## Array;

typedef const char *str_t;

MAKE_STRUCT_ARRAY(Plane)
MAKE_STRUCT_ARRAY(Vector2)
MAKE_STRUCT_ARRAY(Vector3)
MAKE_STRUCT_ARRAY(Rect2)
MAKE_STRUCT_ARRAY(Transform2D)
MAKE_STRUCT_ARRAY(Basis)
MAKE_STRUCT_ARRAY(Quat)
MAKE_STRUCT_ARRAY(Transform)
MAKE_STRUCT_ARRAY(AABB)
MAKE_STRUCT_ARRAY(Color)
MAKE_STRUCT_ARRAY(uint32_t)
MAKE_STRUCT_ARRAY(uint8_t)
MAKE_STRUCT_ARRAY(double)
MAKE_STRUCT_ARRAY(float)
MAKE_STRUCT_ARRAY(str_t)


#pragma pack(pop)
#endif