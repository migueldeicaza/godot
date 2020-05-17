#include <stdio.h>
#include "Defs.h"
void *
godot_icall_Object_Ctor (void *x)
{
printf ("Got %p\n", x);
return NULL;
}
