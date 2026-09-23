#include "protocol/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
static void quote(const char *s){putchar('"');for(;*s;s++){if(*s=='"'||*s=='\\')putchar('\\');putchar(*s);}putchar('"');}
int main(int argc,char **argv){
    char line[4096];int start=argc==3&&!strcmp(argv[1],"--modbus-start")?atoi(argv[2]):-1;
    while(fgets(line,sizeof(line),stdin)){uint8_t bytes[1024];size_t count=0;int high=-1;bool bad=false;
        for(char *p=line;*p;p++){if(isspace((unsigned char)*p))continue;int x=*p>='0'&&*p<='9'?*p-'0':*p>='a'&&*p<='f'?*p-'a'+10:*p>='A'&&*p<='F'?*p-'A'+10:-1;if(x<0){bad=true;break;}if(high<0)high=x;else{if(count==sizeof(bytes)){bad=true;break;}bytes[count++]=(high<<4)|x;high=-1;}}
        ghost_values_t v;bool valid=!bad&&high<0&&(start>=0?ghost_modbus_response(bytes,count,start,&v):ghost_inteless_decode(bytes,count,&v));
        if(!valid){puts("{\"decoded\":false}");continue;}
        printf("{\"decoded\":true,\"serial\":");quote(v.serial);printf(",\"values\":{");bool first=true;
        for(size_t i=0;i<ghost_field_count;i++)if(v.valid&(UINT64_C(1)<<i)){if(!first)putchar(',');first=false;quote(ghost_fields[i].id);printf(":{\"value\":%.12g,\"unit\":",v.value[i]);quote(ghost_fields[i].unit);printf(",\"register\":%d}",ghost_fields[i].reg[0]);}puts("}}");
    }return 0;
}
