#include <windows.h>
#include <stdio.h>
int main(void){
  DISPLAY_DEVICEA d; d.cb=sizeof(d); int i=0, n=0;
  printf("--- display adapters ---\n");
  while(EnumDisplayDevicesA(NULL,i,&d,0)){
    printf("adapter[%d] name='%s' desc='%s' flags=0x%X\n", i, d.DeviceName, d.DeviceString, d.StateFlags);
    ++i; ++n;
  }
  DISPLAY_DEVICEA m; m.cb=sizeof(m); int j=0;
  printf("--- monitors ---\n");
  while(EnumDisplayDevicesA(NULL,j,&m,0)){ j++; }
  printf("total_adapters=%d\n", n);
  DEVMODEA dm; dm.dmSize=sizeof(dm);
  if(EnumDisplaySettingsA(NULL,ENUM_CURRENT_SETTINGS,&dm))
    printf("mode=%lux%lu @%luHz bpp=%lu\n",(unsigned long)dm.dmPelsWidth,(unsigned long)dm.dmPelsHeight,(unsigned long)dm.dmDisplayFrequency,(unsigned long)dm.dmBitsPerPel);
  else printf("EnumDisplaySettings FAILED\n");
  return 0;
}
