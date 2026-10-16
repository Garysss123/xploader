ENTRY(_start)
SECTIONS {
  . = 0;
  .text : { KEEP(*(.text.entry)) *(.text.vectors) *(.text*) }
  .rodata : { *(.rodata*) }
  .rela.dyn : { __rela_start = .; *(.rela*) __rela_end = .; }
  .data : { *(.data*) }
  . = ALIGN(16);
  __bss_start = .;
  .bss : { *(.bss*) *(COMMON) }
  __bss_end = .;
  ASSERT(__bss_end <= 327680, "Native menu exceeds reserved appended region")
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
