"""Exercise released programs on generated files, outside user data."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def pdf():
    objects=[b"<< /Type /Catalog /Pages 2 0 R >>",b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"]
    stream=b"BT /F1 12 Tf 20 100 Td (MDO optional tool PDF fixture) Tj ET"
    objects.append(b"<< /Length "+str(len(stream)).encode()+b" >>\nstream\n"+stream+b"\nendstream")
    out=bytearray(b"%PDF-1.4\n");offsets=[0]
    for i,obj in enumerate(objects,1):
        offsets.append(len(out));out.extend(str(i).encode()+b" 0 obj\n"+obj+b"\nendobj\n")
    start=len(out);out.extend(b"xref\n0 6\n0000000000 65535 f \n")
    for off in offsets[1:]:out.extend(f"{off:010d} 00000 n \n".encode())
    out.extend(b"trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n"+str(start).encode()+b"\n%%EOF\n");return out


def run(stage,platform,only):
    rows=json.loads((stage/"input.json").read_text(encoding="utf-8"));results=[]
    with tempfile.TemporaryDirectory(prefix="mdo tools 中文 ") as temp:
        work=Path(temp);env=dict(os.environ)
        if os.name=="nt":env["PATH"]=str(Path(os.environ["SystemRoot"])/"System32")
        env.update(GIT_CONFIG_NOSYSTEM="1",GIT_CONFIG_GLOBAL=str(work/"empty-gitconfig"),GIT_TERMINAL_PROMPT="0")
        (work/"empty-gitconfig").write_text("");(work/"fixture.pdf").write_bytes(pdf())
        def execute(row,name,*args):
            command=next(c for c in row["commands"] if c["name"]==name)
            entry=stage/platform/row["id"]/command["entry"]
            if not entry.is_file():raise AssertionError(entry)
            p=subprocess.run([str(entry.resolve()),*command.get("args",[]),*map(str,args)],cwd=work,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=90)
            assert p.returncode==0,(name,p.returncode,p.stderr.decode(errors="replace")[-2000:])
            return p.stdout.decode("utf-8",errors="replace")
        for row in rows:
            name=row["id"]
            if row["platform"]!=platform or (only and name not in only):continue
            if name=="git":
                execute(row,"git","init","repository")
                (work/"repository/中文.txt").write_text("mdo fixture\n",encoding="utf-8")
                execute(row,"git","-C","repository","add","中文.txt")
                execute(row,"git","-C","repository","-c","user.name=Fixture","-c","user.email=fixture@example.invalid","commit","-m","fixture")
                assert "fixture" in execute(row,"git","-C","repository","log","-1","--format=%s")
                execute(row,"git","clone","--no-hardlinks","repository","cloned")
                assert (work/"cloned/中文.txt").read_text()=="mdo fixture\n"
            elif name=="sqlite":
                out=execute(row,"sqlite3",":memory:","CREATE VIRTUAL TABLE docs USING fts5(body); INSERT INTO docs VALUES('mdo'); SELECT body FROM docs WHERE docs MATCH 'mdo'; SELECT json_extract('{\"a\":42}','$.a');")
                assert out.splitlines()==["mdo","42"],out
            elif name=="poppler":
                assert "MDO optional tool PDF fixture" in execute(row,"pdftotext","fixture.pdf","-")
                assert "Pages:" in execute(row,"pdfinfo","fixture.pdf")
                execute(row,"pdftoppm","-scale-to","128","-png","-singlefile","fixture.pdf","render")
                assert (work/"render.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
            elif name=="imagemagick":
                execute(row,"magick","-size","64x64","gradient:red-blue","-resize","32x24!","image.png")
                assert execute(row,"magick","identify","-format","%w %h","image.png")=="32 24"
                execute(row,"magick","image.png","image.webp")
                assert (work/"image.webp").read_bytes()[8:12]==b"WEBP"
            elif name=="ffmpeg":
                encoders=execute(row,"ffmpeg","-hide_banner","-encoders")
                assert all(v in encoders for v in ("libx264","libx265","libvpx-vp9","libopus","libmp3lame","libaom-av1"))
                filters=execute(row,"ffmpeg","-hide_banner","-filters");assert all(v in filters for v in ("drawtext","subtitles","scale","crop"))
                assert "https" in execute(row,"ffmpeg","-hide_banner","-protocols")
                execute(row,"ffmpeg","-v","error","-f","lavfi","-i","testsrc2=size=64x64:rate=10","-f","lavfi","-i","sine=frequency=440:sample_rate=48000","-t","0.5","-c:v","libx264","-threads","1","-c:a","aac","-y","video.mp4")
                info=json.loads(execute(row,"ffprobe","-v","error","-show_streams","-of","json","video.mp4"))
                assert {s["codec_name"] for s in info["streams"]}=={"h264","aac"}
                execute(row,"ffmpeg","-v","error","-i","video.mp4","-frames:v","1","-y","frame.png")
                assert (work/"frame.png").read_bytes().startswith(b"\x89PNG")
            results.append(dict(id=name,platform=platform,passed=True))
            print("PASS",name,platform,flush=True)
    return results


if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("--stage",type=Path,required=True);p.add_argument("--platform",required=True);p.add_argument("--only",nargs="*");p.add_argument("--report",type=Path)
    a=p.parse_args();rows=run(a.stage.resolve(),a.platform,a.only)
    if a.report:a.report.write_text(json.dumps(rows,indent=2)+"\n")
