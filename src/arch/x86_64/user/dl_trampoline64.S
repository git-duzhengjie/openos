/*
 * dl_trampoline64.S - M5.1d 用户态惰性绑定 trampoline (_dl_runtime_resolve)
 *
 * x86_64 psABI 惰性绑定入栈约定（进入本 trampoline 时的栈布局）：
 *     8(%rsp) = reloc_index   （PLT stub 压入的 JMPREL 下标）
 *     0(%rsp) = link_map*     （PLT[0] 压入的 GOT[1] 值）
 *   （返回地址即调用者，已经不在栈上——PLT stub 用 jmp 而非 call）
 *
 * 职责：
 *   1. 保存所有会破坏的寄存器（含 SysV 传参寄存器，因为解析完要 jmp 到目标函数，
 *      目标函数期望原始参数完好）。
 *   2. 发起 SYS_DL_RESOLVE(link_map, reloc_index)，内核回填 GOT 并返回目标地址(rax)。
 *   3. 恢复寄存器，弹掉 link_map/reloc_index 两个 8 字节，jmp 到目标地址。
 *
 * 注意：syscall 指令会破坏 rcx/r11，且内核约定 rax=返回值。我们把目标地址
 * 从 rax 转存到一个调用者-保存且非传参的临时（这里用 r11，syscall 后本就作废）。
 */

.section .text,"ax"
.code64

.global _dl_runtime_resolve
.extern openos64_dl_resolve      /* C 封装：long openos64_dl_resolve(lm, idx) -> 目标地址 */

_dl_runtime_resolve:
    /*
     * 进入时:  0(%rsp)=link_map, 8(%rsp)=reloc_index
     * 先保存所有传参 / caller-saved 寄存器，保证解析后跳转目标函数时参数完好。
     * 保存顺序（压栈）后面按逆序弹出。
     */
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %r8
    pushq %r9
    pushq %rax
    pushq %r10
    pushq %r11

    /*
     * 取参数：此刻栈顶到原始 link_map 的偏移 = 9 个已压寄存器 * 8 = 72。
     *   link_map   位于 72(%rsp)
     *   reloc_index位于 80(%rsp)
     */
    movq  72(%rsp), %rdi        /* a0 = link_map* */
    movq  80(%rsp), %rsi        /* a1 = reloc_index */

    /* 16 字节栈对齐：当前压了 9 项(奇数)，call 前需对齐到 16。
     * 进入 trampoline 时 rsp 满足 (rsp+16)%16==0（两个 push 由 PLT 完成）。
     * 这里再压 9 项 => rsp%16==8，call 会再压 8 => 被调用函数内 rsp%16==0，符合 ABI，
     * 无需额外调整。*/
    call  openos64_dl_resolve   /* 返回目标地址于 %rax */

    /* 把目标地址暂存到 %r11（下面会恢复其它寄存器，r11 最后恢复前用掉） */
    movq  %rax, %r11

    /* 逆序恢复（注意 r11 要用作跳转目标，最后单独处理） */
    /* 先把栈上保存的 r11 值丢弃到 rax 临时？——改用策略：
     * 我们恢复除 r11 外的所有寄存器，r11 保留为目标地址。
     * 栈上布局(从栈顶)：r11,r10,rax,r9,r8,rcx,rdx,rsi,rdi
     */
    addq  $8, %rsp              /* 跳过栈上保存的 r11（我们要用 r11 存目标地址） */
    popq  %r10
    popq  %rax
    popq  %r9
    popq  %r8
    popq  %rcx
    popq  %rdx
    popq  %rsi
    popq  %rdi

    /* 此刻栈顶: 0(%rsp)=link_map, 8(%rsp)=reloc_index，弹掉这 16 字节 */
    addq  $16, %rsp

    /* 跳转到解析出的目标函数（参数寄存器已全部还原，返回地址为原调用者） */
    jmp   *%r11
