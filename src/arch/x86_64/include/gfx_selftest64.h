#ifndef OPENOS_ARCH_X86_64_GFX_SELFTEST64_H
#define OPENOS_ARCH_X86_64_GFX_SELFTEST64_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M6.3 graphics acceleration selftest. Verifies the framebuffer row-blit
 * primitive (framebuffer_blit_row): ROW_BLIT capability bit present, NULL /
 * out-of-bounds inputs are rejected safely, horizontal clipping returns the
 * correct pixel count, and a written row reads back correctly from VRAM.
 * Returns non-zero on PASS. Safe after framebuffer_init().
 */
int arch_x86_64_gfx_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_ARCH_X86_64_GFX_SELFTEST64_H */
