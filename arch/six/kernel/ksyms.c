
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/user.h>
#include <linux/elfcore.h>


static struct symbol_table arch_symbol_table;


void arch_syms_export(void)
{
        register_symtab(&arch_symbol_table);
}

