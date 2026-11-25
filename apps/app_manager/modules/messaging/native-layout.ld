/* Match section-loader rounding and the existing sibling standalone modules. */
SECTIONS
{
    .rodata ALIGN(4) : ALIGN(4) { *(.rodata .rodata.* .gnu.linkonce.r.*) }
}
INSERT AFTER .text;
