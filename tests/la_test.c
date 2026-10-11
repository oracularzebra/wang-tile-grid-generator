#include <stdio.h>
#include <math.h>
#include "../la.c"

static int check(const char *name, float got, float want)
{
    if (fabsf(got - want) > 1e-5f) {
        printf("FAIL %s: got %g, want %g\n", name, got, want);
        return 1;
    }
    return 0;
}

int main(void)
{
    int bad = 0;
    bad += check("vec2f_len", vec2f_len(vec2f(3, 4)), 5);
    bad += check("vec3f_sqrlen", vec3f_sqrlen(vec3f(1, 2, 2)), 9);
    bad += check("vec3f_len", vec3f_len(vec3f(1, 2, 2)), 3);
    bad += check("vec4f_sqrlen", vec4f_sqrlen(vec4f(1, 1, 1, 1)), 4);
    bad += check("vec4f_len", vec4f_len(vec4f(2, 2, 2, 2)), 4);
    bad += check("vec4f_sum", vec4f_sum(vec4fs(1), vec4fs(2)).c[3], 3);
    bad += check("vec4f_mul", vec4f_mul(vec4fs(2), vec4fs(3)).c[0], 6);
    if (!bad) printf("la ok\n");
    return bad != 0;
}
