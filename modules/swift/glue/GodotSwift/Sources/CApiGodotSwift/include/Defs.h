//#include "glue_header.h"
// #include "core/class_db.h"
// #include "core/method_bind.h"
#include "modules/swift/swift_gd/gd_swift_marshal_c_api.h"
#include <stdlib.h>

typedef struct SwiftObject SwiftObject;
typedef struct Object Object;
typedef char SwiftString;
typedef char SwiftArray;
typedef struct Array Array;
typedef struct Dictionary Dictionary;
typedef struct NodePath NodePath;
typedef struct RID RID; 

typedef struct MethodBind MethodBind;

void *godot_icall_Object_Ctor (SwiftObject *swiftPtr);
#include "../../../../swift_glue.gen.inc"
