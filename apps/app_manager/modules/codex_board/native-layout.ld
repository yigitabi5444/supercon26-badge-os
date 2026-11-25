/* The section loader maps .text with 4-byte rounding and requires aligned .rodata. */
SECTIONS
{
    .rodata ALIGN(4) : ALIGN(4) { *(.rodata .rodata.* .gnu.linkonce.r.*) }
}
INSERT AFTER .text;
