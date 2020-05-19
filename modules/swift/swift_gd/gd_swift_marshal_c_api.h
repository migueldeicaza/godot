#ifndef GDSWIFTMARSHAL_C_API_H
#define GDSWIFTMARSHAL_C_API_H

// This is broken, need to include the proper header files,
// but swift chokes on C++ header files
typedef float real_t;
#pragma pack(push, 1)

typedef struct {
	real_t x, y;

	// static _FORCE_INLINE_ Vector2 convert_to(const Vector2 &p_from) {
	// 	return Vector2(p_from.x, p_from.y);
	// }

	// static _FORCE_INLINE_ Vector2 convert_from(const Vector2 &p_from) {
	// 	Vector2 ret = { p_from.x, p_from.y };
	// 	return ret;
	// }
} Vector2;

typedef struct {
	Vector2 position;
	Vector2 size;

	// static _FORCE_INLINE_ Rect2 convert_to(const Rect2 &p_from) {
	// 	return Rect2(Vector2::convert_to(p_from.position),
	// 			Vector2::convert_to(p_from.size));
	// }

	// static _FORCE_INLINE_ Rect2 convert_from(const Rect2 &p_from) {
	// 	Rect2 ret = { Vector2::convert_from(p_from.position), Vector2::convert_from(p_from.size) };
	// 	return ret;
	// }
} Rect2;

typedef struct {
	Vector2 elements[3];

	// static _FORCE_INLINE_ Transform2D convert_to(const Transform2D &p_from) {
	// 	return Transform2D(p_from.elements[0].x, p_from.elements[0].y,
	// 			p_from.elements[1].x, p_from.elements[1].y,
	// 			p_from.elements[2].x, p_from.elements[2].y);
	// }

	// static _FORCE_INLINE_ Transform2D convert_from(const Transform2D &p_from) {
	// 	Transform2D ret = {
	// 		Vector2::convert_from(p_from.elements[0]),
	// 		Vector2::convert_from(p_from.elements[1]),
	// 		Vector2::convert_from(p_from.elements[2])
	// 	};
	// 	return ret;
	
} Transform2D;

typedef struct {
	real_t x, y, z;

	// static _FORCE_INLINE_ Vector3 convert_to(const Vector3 &p_from) {
	// 	return Vector3(p_from.x, p_from.y, p_from.z);
	// }

	// static _FORCE_INLINE_ Vector3 convert_from(const Vector3 &p_from) {
	// 	Vector3 ret = { p_from.x, p_from.y, p_from.z };
	// 	return ret;
	// }
} Vector3;

typedef struct  {
	Vector3 elements[3];

	// static _FORCE_INLINE_ Basis convert_to(const Basis &p_from) {
	// 	return Basis(Vector3::convert_to(p_from.elements[0]),
	// 			Vector3::convert_to(p_from.elements[1]),
	// 			Vector3::convert_to(p_from.elements[2]));
	// }

	// static _FORCE_INLINE_ Basis convert_from(const Basis &p_from) {
	// 	Basis ret = {
	// 		Vector3::convert_from(p_from.elements[0]),
	// 		Vector3::convert_from(p_from.elements[1]),
	// 		Vector3::convert_from(p_from.elements[2])
	// 	};
	// 	return ret;
	// }
} Basis;

typedef struct {
	real_t x, y, z, w;

// 	static _FORCE_INLINE_ Quat convert_to(const Quat &p_from) {
// 		return Quat(p_from.x, p_from.y, p_from.z, p_from.w);
// 	}

// 	static _FORCE_INLINE_ Quat convert_from(const Quat &p_from) {
// 		Quat ret = { p_from.x, p_from.y, p_from.z, p_from.w };
// 		return ret;
// 	}
} Quat;

typedef struct  {
	Basis basis;
	Vector3 origin;

	// static _FORCE_INLINE_ Transform convert_to(const Transform &p_from) {
	// 	return Transform(Basis::convert_to(p_from.basis), Vector3::convert_to(p_from.origin));
	// }

	// static _FORCE_INLINE_ Transform convert_from(const Transform &p_from) {
	// 	Transform ret = { Basis::convert_from(p_from.basis), Vector3::convert_from(p_from.origin) };
	// 	return ret;
	// }
}Transform;

typedef struct  {
	Vector3 position;
	Vector3 size;

	// static _FORCE_INLINE_ AABB convert_to(const AABB &p_from) {
	// 	return AABB(Vector3::convert_to(p_from.position), Vector3::convert_to(p_from.size));
	// }

	// static _FORCE_INLINE_ AABB convert_from(const AABB &p_from) {
	// 	AABB ret = { Vector3::convert_from(p_from.position), Vector3::convert_from(p_from.size) };
	// 	return ret;
	// }
}AABB;

typedef struct  {
	float r, g, b, a;

	// static _FORCE_INLINE_ Color convert_to(const Color &p_from) {
	// 	return Color(p_from.r, p_from.g, p_from.b, p_from.a);
	// }

	// static _FORCE_INLINE_ Color convert_from(const Color &p_from) {
	// 	Color ret = { p_from.r, p_from.g, p_from.b, p_from.a };
	// 	return ret;
	// }
}Color;

typedef struct  {
	Vector3 normal;
	real_t d;

	// static _FORCE_INLINE_ Plane convert_to(const Plane &p_from) {
	// 	return Plane(Vector3::convert_to(p_from.normal), p_from.d);
	// }

	// static _FORCE_INLINE_ Plane convert_from(const Plane &p_from) {
	// 	Plane ret = { Vector3::convert_from(p_from.normal), p_from.d };
	// 	return ret;
	// }
}Plane;

#pragma pack(pop)
#endif