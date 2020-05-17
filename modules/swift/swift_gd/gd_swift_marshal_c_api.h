#ifndef GDSWIFTMARSHAL_C_API_H
#define GDSWIFTMARSHAL_C_API_H

// This is broken, need to include the proper header files,
// but swift chokes on C++ header files
typedef float real_t;
#pragma pack(push, 1)

typedef struct {
	real_t x, y;

	// static _FORCE_INLINE_ Vector2 convert_to(const M_Vector2 &p_from) {
	// 	return Vector2(p_from.x, p_from.y);
	// }

	// static _FORCE_INLINE_ M_Vector2 convert_from(const Vector2 &p_from) {
	// 	M_Vector2 ret = { p_from.x, p_from.y };
	// 	return ret;
	// }
} M_Vector2;

typedef struct {
	M_Vector2 position;
	M_Vector2 size;

	// static _FORCE_INLINE_ Rect2 convert_to(const M_Rect2 &p_from) {
	// 	return Rect2(M_Vector2::convert_to(p_from.position),
	// 			M_Vector2::convert_to(p_from.size));
	// }

	// static _FORCE_INLINE_ M_Rect2 convert_from(const Rect2 &p_from) {
	// 	M_Rect2 ret = { M_Vector2::convert_from(p_from.position), M_Vector2::convert_from(p_from.size) };
	// 	return ret;
	// }
} M_Rect2;

typedef struct {
	M_Vector2 elements[3];

	// static _FORCE_INLINE_ Transform2D convert_to(const M_Transform2D &p_from) {
	// 	return Transform2D(p_from.elements[0].x, p_from.elements[0].y,
	// 			p_from.elements[1].x, p_from.elements[1].y,
	// 			p_from.elements[2].x, p_from.elements[2].y);
	// }

	// static _FORCE_INLINE_ M_Transform2D convert_from(const Transform2D &p_from) {
	// 	M_Transform2D ret = {
	// 		M_Vector2::convert_from(p_from.elements[0]),
	// 		M_Vector2::convert_from(p_from.elements[1]),
	// 		M_Vector2::convert_from(p_from.elements[2])
	// 	};
	// 	return ret;
	
} M_Transform2D;

typedef struct {
	real_t x, y, z;

	// static _FORCE_INLINE_ Vector3 convert_to(const M_Vector3 &p_from) {
	// 	return Vector3(p_from.x, p_from.y, p_from.z);
	// }

	// static _FORCE_INLINE_ M_Vector3 convert_from(const Vector3 &p_from) {
	// 	M_Vector3 ret = { p_from.x, p_from.y, p_from.z };
	// 	return ret;
	// }
} M_Vector3;

typedef struct  {
	M_Vector3 elements[3];

	// static _FORCE_INLINE_ Basis convert_to(const M_Basis &p_from) {
	// 	return Basis(M_Vector3::convert_to(p_from.elements[0]),
	// 			M_Vector3::convert_to(p_from.elements[1]),
	// 			M_Vector3::convert_to(p_from.elements[2]));
	// }

	// static _FORCE_INLINE_ M_Basis convert_from(const Basis &p_from) {
	// 	M_Basis ret = {
	// 		M_Vector3::convert_from(p_from.elements[0]),
	// 		M_Vector3::convert_from(p_from.elements[1]),
	// 		M_Vector3::convert_from(p_from.elements[2])
	// 	};
	// 	return ret;
	// }
} M_Basis;

typedef struct {
	real_t x, y, z, w;

// 	static _FORCE_INLINE_ Quat convert_to(const M_Quat &p_from) {
// 		return Quat(p_from.x, p_from.y, p_from.z, p_from.w);
// 	}

// 	static _FORCE_INLINE_ M_Quat convert_from(const Quat &p_from) {
// 		M_Quat ret = { p_from.x, p_from.y, p_from.z, p_from.w };
// 		return ret;
// 	}
} M_Quat;

typedef struct  {
	M_Basis basis;
	M_Vector3 origin;

	// static _FORCE_INLINE_ Transform convert_to(const M_Transform &p_from) {
	// 	return Transform(M_Basis::convert_to(p_from.basis), M_Vector3::convert_to(p_from.origin));
	// }

	// static _FORCE_INLINE_ M_Transform convert_from(const Transform &p_from) {
	// 	M_Transform ret = { M_Basis::convert_from(p_from.basis), M_Vector3::convert_from(p_from.origin) };
	// 	return ret;
	// }
}M_Transform;

typedef struct  {
	M_Vector3 position;
	M_Vector3 size;

	// static _FORCE_INLINE_ AABB convert_to(const M_AABB &p_from) {
	// 	return AABB(M_Vector3::convert_to(p_from.position), M_Vector3::convert_to(p_from.size));
	// }

	// static _FORCE_INLINE_ M_AABB convert_from(const AABB &p_from) {
	// 	M_AABB ret = { M_Vector3::convert_from(p_from.position), M_Vector3::convert_from(p_from.size) };
	// 	return ret;
	// }
}M_AABB;

typedef struct  {
	float r, g, b, a;

	// static _FORCE_INLINE_ Color convert_to(const M_Color &p_from) {
	// 	return Color(p_from.r, p_from.g, p_from.b, p_from.a);
	// }

	// static _FORCE_INLINE_ M_Color convert_from(const Color &p_from) {
	// 	M_Color ret = { p_from.r, p_from.g, p_from.b, p_from.a };
	// 	return ret;
	// }
}M_Color;

typedef struct  {
	M_Vector3 normal;
	real_t d;

	// static _FORCE_INLINE_ Plane convert_to(const M_Plane &p_from) {
	// 	return Plane(M_Vector3::convert_to(p_from.normal), p_from.d);
	// }

	// static _FORCE_INLINE_ M_Plane convert_from(const Plane &p_from) {
	// 	M_Plane ret = { M_Vector3::convert_from(p_from.normal), p_from.d };
	// 	return ret;
	// }
}M_Plane;

#pragma pack(pop)
#endif