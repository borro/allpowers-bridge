"""Thin C ABI bridge. All protocol decoding is compiled from core/src."""
from __future__ import annotations
import ctypes as C
from pathlib import Path
import sys
import os
import platform

FIELDS=('abi_version model known main_seen settings_seen fresh soc input_w output_w runtime_s source source_raw ac_output usb_output dc_output main_age_ms settings_age_ms valid_frames invalid_frames').split()
class Snapshot(C.Structure):
    _fields_=[(name,C.c_uint32) for name in FIELDS]

def library_path():
    if os.environ.get('ALLPOWERS_CORE_LIBRARY'):
        return Path(os.environ['ALLPOWERS_CORE_LIBRARY'])
    base=Path(getattr(sys,'_MEIPASS',Path(__file__).parent))
    name='allpowers_core.dll' if sys.platform=='win32' else 'liballpowers_core.so'
    bundled=base/'native'/name
    return bundled if bundled.exists() else Path(__file__).resolve().parents[2]/'build/native'/platform.system()/name

class Core:
    def __init__(self,model=0,path=None):
        self.lib=C.CDLL(str(path or library_path()))
        lib=self.lib
        lib.ap_abi_version.restype=C.c_uint32
        if lib.ap_abi_version()!=1:raise RuntimeError('Несовместимая версия общего ядра')
        lib.ap_create.argtypes=[C.c_uint32];lib.ap_create.restype=C.c_void_p
        lib.ap_destroy.argtypes=[C.c_void_p];lib.ap_reset.argtypes=[C.c_void_p,C.c_uint32]
        lib.ap_receive.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_uint32]
        lib.ap_get_snapshot.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(Snapshot),C.c_uint32]
        lib.ap_model_hint.argtypes=[C.c_char_p];lib.ap_model_hint.restype=C.c_uint32
        lib.ap_uuid.argtypes=[C.c_uint32];lib.ap_uuid.restype=C.c_char_p
        lib.ap_catalog_count.restype=C.c_uint32
        lib.ap_catalog_name.argtypes=[C.c_uint32];lib.ap_catalog_name.restype=C.c_char_p
        lib.ap_catalog_model.argtypes=[C.c_uint32];lib.ap_catalog_model.restype=C.c_uint32
        lib.ap_poll.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32];lib.ap_poll.restype=C.c_uint32
        lib.ap_poll_interval_ms.restype=C.c_uint32;lib.ap_reconnect_silence_ms.restype=C.c_uint32
        self.handle=lib.ap_create(model)
        if not self.handle:raise MemoryError('Не удалось создать сессию')
        if not lib.ap_self_test():self.close();raise RuntimeError('Самопроверка общего ядра не прошла')
    def close(self):
        if self.handle:self.lib.ap_destroy(self.handle);self.handle=None
    def __del__(self):
        if getattr(self,'handle',None):self.close()
    def reset(self,model):self.lib.ap_reset(self.handle,model)
    def receive(self,data,now):
        payload=C.create_string_buffer(bytes(data))
        return bool(self.lib.ap_receive(self.handle,payload,len(data),now&0xffffffff))
    def snapshot(self,now):
        out=Snapshot()
        if not self.lib.ap_get_snapshot(self.handle,now&0xffffffff,C.byref(out),C.sizeof(out)):raise RuntimeError('Ошибка ABI ядра')
        return {key:int(getattr(out,key)) for key in FIELDS}
    def hint(self,name):return self.lib.ap_model_hint(name.encode('utf-8'))
    def uuid(self,kind):return self.lib.ap_uuid(kind).decode('ascii')
    def poll(self):
        out=C.create_string_buffer(64);size=self.lib.ap_poll(self.handle,out,len(out));return out.raw[:size]
    @property
    def poll_seconds(self):return self.lib.ap_poll_interval_ms()/1000
    @property
    def silence_seconds(self):return self.lib.ap_reconnect_silence_ms()/1000
    def catalog(self):return [(self.lib.ap_catalog_name(i).decode(),self.lib.ap_catalog_model(i)) for i in range(self.lib.ap_catalog_count())]
