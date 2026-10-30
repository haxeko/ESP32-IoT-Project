/* Two-layer grid A* used by autoroute.py (built at run time with cc -O2 -shared).
 *
 * State = (layer, cell, direction). Moves: 8 neighbours on the same layer, or a via
 * to the other layer on the same cell. Turns of 45/90 degrees cost extra, sharper
 * turns are not allowed (keeps tracks clean). Returns the path as encoded cells
 * (layer * n + j * nx + i), goal first.
 */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float f; int32_t s; } item_t;

static item_t *heap = NULL;
static long heap_n = 0, heap_cap = 0;

static void hpush(float f, int32_t s) {
    if (heap_n >= heap_cap) {
        heap_cap = heap_cap ? heap_cap * 2 : 1 << 20;
        heap = (item_t *)realloc(heap, heap_cap * sizeof(item_t));
    }
    long i = heap_n++;
    while (i > 0) {
        long p = (i - 1) >> 1;
        if (heap[p].f <= f) break;
        heap[i] = heap[p];
        i = p;
    }
    heap[i].f = f;
    heap[i].s = s;
}

static item_t hpop(void) {
    item_t top = heap[0];
    item_t last = heap[--heap_n];
    long i = 0;
    for (;;) {
        long c = 2 * i + 1;
        if (c >= heap_n) break;
        if (c + 1 < heap_n && heap[c + 1].f < heap[c].f) c++;
        if (heap[c].f >= last.f) break;
        heap[i] = heap[c];
        i = c;
    }
    if (heap_n > 0) heap[i] = last;
    return top;
}

static const int DX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
static const int DY[8] = {0, 1, 1, 1, 0, -1, -1, -1};

int astar(int nx, int ny, const uint8_t *free0, const uint8_t *free1, const uint8_t *viaok,
          const float *cost0, const float *cost1, const uint8_t *goal0, const uint8_t *goal1,
          const int32_t *src, int nsrc, int gx0, int gy0, int gx1, int gy1,
          float via_cost, float turn45, float turn90, float *g, int32_t *from,
          int32_t *path, int maxpath, long max_expand) {
    const long n = (long)nx * ny;
    const long ns = 2 * n * 9;
    for (long k = 0; k < ns; k++) g[k] = INFINITY;
    heap_n = 0;
    const uint8_t *fr[2] = {free0, free1};
    const float *co[2] = {cost0, cost1};
    const uint8_t *go[2] = {goal0, goal1};
    for (int k = 0; k < nsrc; k++) {
        long c = src[k];
        long s = c * 9 + 8;
        g[s] = 0.0f;
        from[s] = -1;
        hpush(0.0f, (int32_t)s);
    }
    long expanded = 0;
    while (heap_n > 0) {
        item_t it = hpop();
        long s = it.s;
        long c = s / 9;
        int d = (int)(s % 9);
        int L = (int)(c / n);
        long cell = c % n;
        int j = (int)(cell / nx), i = (int)(cell % nx);
        float gs = g[s];
        float hx = (float)(i < gx0 ? gx0 - i : (i > gx1 ? i - gx1 : 0));
        float hy = (float)(j < gy0 ? gy0 - j : (j > gy1 ? j - gy1 : 0));
        float h = (hx > hy) ? hx + 0.41421356f * hy : hy + 0.41421356f * hx;
        if (it.f > gs + h + 1e-3f) continue; /* stale */
        if (go[L][cell]) {
            int len = 0;
            long t = s;
            while (t >= 0 && len < maxpath) {
                path[len++] = (int32_t)(t / 9);
                t = from[t];
            }
            return len;
        }
        if (++expanded > max_expand) return -2;
        for (int k = 0; k < 8; k++) {
            if (d != 8) {
                int dd = abs(k - d);
                if (dd > 4) dd = 8 - dd;
                if (dd >= 3) continue;
            }
            int ii = i + DX[k], jj = j + DY[k];
            if (ii < 0 || jj < 0 || ii >= nx || jj >= ny) continue;
            long nc = (long)jj * nx + ii;
            if (!fr[L][nc]) continue;
            float step = (k & 1) ? 1.41421356f : 1.0f;
            float cost = step * co[L][nc];
            if (d != 8 && d != k) {
                int dd = abs(k - d);
                if (dd > 4) dd = 8 - dd;
                cost += (dd == 1) ? turn45 : turn90;
            }
            long ns2 = ((long)L * n + nc) * 9 + k;
            float ng = gs + cost;
            if (ng < g[ns2]) {
                g[ns2] = ng;
                from[ns2] = (int32_t)s;
                float hx2 = (float)(ii < gx0 ? gx0 - ii : (ii > gx1 ? ii - gx1 : 0));
                float hy2 = (float)(jj < gy0 ? gy0 - jj : (jj > gy1 ? jj - gy1 : 0));
                float h2 = (hx2 > hy2) ? hx2 + 0.41421356f * hy2 : hy2 + 0.41421356f * hx2;
                hpush(ng + h2, (int32_t)ns2);
            }
        }
        if (viaok[cell]) {
            int L2 = 1 - L;
            if (fr[L2][cell]) {
                long ns2 = ((long)L2 * n + cell) * 9 + 8;
                float ng = gs + via_cost;
                if (ng < g[ns2]) {
                    g[ns2] = ng;
                    from[ns2] = (int32_t)s;
                    hpush(ng + h, (int32_t)ns2);
                }
            }
        }
    }
    return -1;
}
