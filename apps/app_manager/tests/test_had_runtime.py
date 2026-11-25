"""Production Hackaday host/job/HTTP/decode and exact module against SDK doubles."""
from pathlib import Path
import gzip,os,struct,subprocess,tempfile,zlib
app=Path(__file__).resolve().parents[1];repo=app.parents[1]
flags=['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-Dtime=am_test_time','-DCONFIG_MBEDTLS_HAVE_TIME_DATE=1',
       '-Wall','-Wextra','-Werror','-pedantic','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread',
       '-Itests/had_stubs','-Itests/native_host_stubs','-Itests/local_https_stubs','-Itests/stubs','-I.',
       '-Imodules/hackaday','-I'+str(repo/'components/os/include'),'-I'+str(repo/'components/stb')]
sources=['native_host.c','native_codex.c','native_hackaday.c','native_messages.c','native_doom.c','native_had_target.c','native_had_http.c',
         'native_had_decode.c','native_had_url.c','package_http.c','package_source.c','txn_json.c','txn_package.c','native_elf.c',
         'tests/native_host_double.c','tests/had_sdk_double.c','tests/test_had_runtime.c','tests/had_module_host.c',
         'modules/hackaday/had_native_controller.c','modules/hackaday/had_feed_port.c','modules/hackaday/had_format.c',
         str(repo/'components/stb/stb_image.c')]
def fixtures(tmp):
    text=b'<rss><channel><item><title>Fixture project</title><dc:creator>Badge OS</dc:creator><pubDate>Tue, 24 Nov 2026</pubDate><content:encoded><![CDATA[<p>Original reader content.</p><img src="https://media.example/image.png" width="160" height="40">]]></content:encoded></item></channel></rss>'
    plain=tmp/'feed.xml';compressed=tmp/'feed.gz';plain.write_bytes(text);compressed.write_bytes(gzip.compress(text,mtime=0))
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    def image(w,h,data):return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(data))+chunk(b'IEND',b'')
    png=tmp/'image.png';large=tmp/'large.png';png.write_bytes(image(2,2,b'\0\0\xff\0\xff\0'));large.write_bytes(image(4096,4096,b''))
    return plain,compressed,png,large
def compile_objects(tmp,extra=()):
    result=[]
    for i,source in enumerate(sources+list(extra)):
        obj=tmp/f'{i}.o';options=['-Dmain=had_runtime_checks'] if extra and source=='tests/test_had_runtime.c' else []
        subprocess.run(flags+options+['-c',source,'-o',str(obj)],cwd=app,check=True);result.append(str(obj))
    return result
if __name__=='__main__':
    with tempfile.TemporaryDirectory(prefix='badge-had-runtime-') as name:
        tmp=Path(name);exe=tmp/'test';inputs=fixtures(tmp)
        subprocess.run(flags+compile_objects(tmp)+['-lm','-o',str(exe)],cwd=app,check=True)
        subprocess.run([str(exe),*map(str,inputs)],check=True,env=dict(os.environ,
            ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
