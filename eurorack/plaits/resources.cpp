volatile int dummy_write = plaits::wav_integrated_waves[0]; 
(void)dummy_write;

/* // Comment out or delete TestLinkerFunction_C
void TestLinkerFunction_C() {
    volatile int x = 55;
    x++;
    (void)x;
}
*/

} // extern "C" closing brace 