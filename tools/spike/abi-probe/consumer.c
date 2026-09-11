/* 纯 C 消费者：证明引擎侧（或工具链侧）只需 C ABI 即可调用仿真核 */
#include <stdio.h>
unsigned long long sc_abi_seed(unsigned long long, unsigned, unsigned);
unsigned sc_abi_roll(unsigned long long, unsigned, unsigned, unsigned long long, unsigned);
unsigned sc_abi_roll_with_frame_dt(unsigned long long, unsigned, unsigned, unsigned long long, unsigned, double);
unsigned sc_abi_schema_version(void);
int main(void){
    unsigned long long ws = 20250910ull; unsigned sid = 0, param = 3; unsigned long long tick = 12345;
    printf("abi_schema_major=%u\n", sc_abi_schema_version());
    unsigned a = sc_abi_roll(ws, sid, param, tick, 0);
    unsigned b = sc_abi_roll(ws, sid, param, tick, 0);
    unsigned c = sc_abi_roll_with_frame_dt(ws, sid, param, tick, 0, 1.0/60.0);
    unsigned d = sc_abi_roll_with_frame_dt(ws, sid, param, tick, 0, 9999.0);
    printf("roll_a=%u roll_b=%u (repeat_identical=%d)\n", a, b, a==b);
    printf("roll_dt60=%u roll_dt9999=%u (frame_dt_irrelevant=%d)\n", c, d, c==d);
    printf("seed64_via_abi=%llu\n", sc_abi_seed(ws, sid, param));
    printf("ABI_PROBE_%s\n", (a==b && c==d) ? "OK" : "FAIL");
    return (a==b && c==d) ? 0 : 1;
}
