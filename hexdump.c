#include <stdio.h>
int main(int argc, char *argv[]) {
    FILE *f = fopen(argv[1], "rb");
    int b, i = 0, c;
    unsigned char row[4];
    printf("Offset  Hex (4 byte = 1 komut)      Ikili                              Aciklama\n");
    printf("------  ---------------------------  ---------------------------------  ----------\n");
    while (1) {
        for (c = 0; c < 4; c++) { b = fgetc(f); if (b == EOF) goto done; row[c] = (unsigned char)b; }
        unsigned int insn = ((unsigned int)row[0]<<24)|((unsigned int)row[1]<<16)|((unsigned int)row[2]<<8)|row[3];
        unsigned int op = (insn>>26)&0x3F;
        unsigned int fd = (insn>>23)&7;
        unsigned int fa = (insn>>20)&7;
        unsigned int fb = (insn>>17)&7;
        int imm = (int)(insn & 0x1FFFF);
        if (imm & 0x10000) imm |= (int)0xFFFF0000;
        const char *mn = "???";
        char detail[64]; detail[0]=0;
        switch(op) {
            case 0x00: mn="NOP";  break;
            case 0x01: mn="ADD";  sprintf(detail,"R%d=R%d+R%d",fd,fa,fb); break;
            case 0x02: mn="SUB";  sprintf(detail,"R%d=R%d-R%d",fd,fa,fb); break;
            case 0x0A: mn="LI";   sprintf(detail,"R%d=%d",fd,imm); break;
            case 0x0D: mn="JNZ";  sprintf(detail,"if R%d!=0: PC+=%d",fd,imm); break;
            case 0x10: mn="OUT";  sprintf(detail,"port[%d]=R%d",imm&0xFF,fd); break;
            case 0x3F: mn="HALT"; break;
        }
        printf("0x%04X  %02X %02X %02X %02X  ", i*4, row[0],row[1],row[2],row[3]);
        /* binary of opcode */
        int bit;
        for(bit=31;bit>=0;bit--) { printf("%c",(insn>>bit)&1?'1':'0'); if(bit==26||bit==23||bit==20||bit==17) printf("."); }
        printf("  %s %s\n", mn, detail);
        i++;
    }
    done: fclose(f); return 0;
}
