ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size = 0x00000000;
_Min_Stack_Size = 0x00000800;

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00010000, LENGTH = 0x00030000
  /* F460JETA APP-A occupies the doubled 192 KiB product slot. */
  RAM   (xrw) : ORIGIN = 0x1FFF8000, LENGTH = 0x0002F000
}

PHDRS
{
  flash   PT_LOAD FLAGS(5); /* PF_R | PF_X */
  ram     PT_LOAD FLAGS(6); /* PF_R | PF_W */
  ram_bss PT_LOAD FLAGS(6); /* PF_R | PF_W */
}

SECTIONS
{
  .vectors :
  {
    . = ALIGN(4);
    KEEP(*(.vectors))
    . = ALIGN(4);
  } > FLASH :flash

  .app_version 0x00011000 :
  {
    . = ALIGN(8);
    KEEP(*(.app_version))
    . = ALIGN(8);
  } > FLASH :flash

  ASSERT(SIZEOF(.app_version) == 48, "app identity section must be exactly 48 bytes")

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
  } > FLASH :flash

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } > FLASH :flash

  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } > FLASH :flash

  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } > FLASH :flash

  .preinit_array (READONLY) :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } > FLASH :flash

  .init_array (READONLY) :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } > FLASH :flash

  .fini_array (READONLY) :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } > FLASH :flash

  /* Symbols required by the F460 assembly startup file. */
  __etext = ALIGN(4);

  .data :
  {
    . = ALIGN(4);
    __data_start__ = .;
    _sdata = .;
    *(.ramfunc)
    *(.ramfunc*)
    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)
    . = ALIGN(4);
    __data_end__ = .;
    _edata = .;
  } > RAM AT > FLASH :ram

  /* No retention-RAM data is used by this application. */
  __etext_ret_ram = __etext;
  __data_start_ret_ram__ = ORIGIN(RAM);
  __data_end_ret_ram__ = ORIGIN(RAM);

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    __bss_start__ = .;
    _sbss = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end__ = .;
    _ebss = .;
  } > RAM :ram_bss

  /* No retention-RAM BSS is used by this application. */
  __bss_start_ret_ram__ = ORIGIN(RAM);
  __bss_end_ret_ram__ = ORIGIN(RAM);

  .noinit (NOLOAD) :
  {
    . = ALIGN(4);
    *(.noinit)
    *(.noinit*)
    . = ALIGN(4);
  } > RAM :NONE

  ._user_heap_stack (NOLOAD):
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } > RAM :NONE

  .ARM.attributes 0 : { *(.ARM.attributes) } :NONE

  /DISCARD/ :
  {
    /* ICG is a device-absolute image at 0x400. The F460 APP is linked at
       0x10000, so the boot image owns this configuration; do not place a
       second copy inside the APP slot. */
    *(.icg_sec)
    *(.note*)
    *(.comment*)
  }
}
