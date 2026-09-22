volatile static unsigned char *sif;

void
_putchar(unsigned char c)
{
  *sif= 'p';
  *sif= c;
}

void
_initEmu(void)
{
  sif= (unsigned char *)0xfefe;
}

void
_exitEmu(void)
{
  *sif= 's';
}
