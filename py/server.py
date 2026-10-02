#!/usr/bin/env python # 
"""
Test WebServices HTTP Server
Usage: server
"""


from flask import Flask
from flask import send_file
from flask import request
from flask import abort
from datetime import datetime
import json
import socket
import os
import hashlib
import io


# The bootloader's pfb_write_to_flash_aligned_256_bytes() can only write whole
# flash pages, so every segment served has to be a multiple of this many bytes.
FLASH_ALIGN_SIZE = 256


app = Flask(__name__, static_url_path='/static')

@app.route('/', methods=['GET', 'POST'])
def root():
    return "<HTML><BODY><H1>Flask Test Page</H1></BODY?</HTML>"


@app.route('/time', methods=['GET', 'POST'])
def time():
    a = datetime.now()
    t = {
        'year': a.year,
        'month': a.month,
        'day': a.day,
        'hour': a.hour,
        'minute': a.minute,
        'second': a.second
        }

    return json.dumps(t)


def serveFirmware(fileName):
    if not os.path.isfile(fileName):
        abort(404)

    segSize = request.args.get('segSize')
    segNum = request.args.get('segNum')
    if (segNum == None):
        segNum = 0
    if (segSize == None):
        print("segSize is None so sending the lot")
        return send_file(fileName)
    else:
        segSize = int(segSize)
        segNum = int(segNum)
        if segSize % FLASH_ALIGN_SIZE:
            abort(400, "segSize must be a multiple of %d" % FLASH_ALIGN_SIZE)
        print("Sending segment %d of size %d"%(segNum, segSize))
        mem=fileSegment(fileName, segSize, segNum)
        return send_file(mem, mimetype="application/octet-stream")

@app.route('/grn', methods=['GET'])
def grn():
    return serveFirmware("../grn/build/src/grn_fota_image.bin")

@app.route('/blu', methods=['GET'])
def blu():
    return serveFirmware("../blu/build/src/blu_fota_image.bin")

@app.route('/srv', methods=['GET'])
def srv():
    return serveFirmware("./server.py")


def fileSegment(filename, segSize, segNum):
    mem = io.BytesIO()

    f = open(filename, "rb", buffering=0)
    if (segNum > 0):
        f.seek(segSize * segNum)

    data = f.read(segSize)
    f.close()

    # The last segment of the image is usually not a whole number of flash
    # pages, so pad it. The padding is written to the download slot but is not
    # part of the firmware image the device validates.
    data += b'\x00' * ((-len(data)) % FLASH_ALIGN_SIZE)

    mem.write(data)

    md = hashlib.md5(mem.getbuffer())
    print("MD5 %s"%(md.hexdigest()))

    mem.seek(0)
    return mem


if __name__ == "__main__":
    app.secret_key = 'DRJONEA'
    app.config['SESSION_TYPE'] = 'filesystem'
    

    hostname = socket.gethostname()
    ip_address = socket.gethostbyname(hostname)
    print(f"Hostname: {hostname}")
    print(f"IP Address: {ip_address}")
    
    app.run(host="0.0.0.0")