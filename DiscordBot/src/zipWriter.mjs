// @ts-check
import * as fs from 'node:fs';
import * as path from 'node:path';
import * as zlib from 'node:zlib';

// Minimal .zip writer (deflate, UTF-8 names) with Node built-ins only (zlib.crc32: Node 22.2+).
// Ported from the build machine's src/zipWriter.ts. No ZIP64: the archive and every entry stay under 4 GB.

const MAX_ZIP32 = 0xffffffff;

/** @param {Date} d */
function dosDateTime(d) {
  const year = Math.max(d.getFullYear(), 1980);
  return {
    time: (d.getHours() << 11) | (d.getMinutes() << 5) | Math.floor(d.getSeconds() / 2),
    date: ((year - 1980) << 9) | ((d.getMonth() + 1) << 5) | d.getDate(),
  };
}

export class ZipWriter {
  /** @param {string} filePath */
  constructor(filePath) {
    this.filePath = filePath;
    this.fd = fs.openSync(filePath, 'w');
    this.offset = 0;
    /** @type {Buffer[]} */
    this.central = [];
    this.count = 0;
  }

  /** @param {Buffer} buf */
  write(buf) {
    fs.writeSync(this.fd, buf);
    this.offset += buf.length;
    if (this.offset > MAX_ZIP32) throw new Error('Zip archive exceeds 4 GB (ZIP64 not supported)');
  }

  /** name: the path inside the zip, "/" separated.  @param {string} name  @param {Buffer} data  @param {Date} modified */
  addFile(name, data, modified) {
    const nameBuf = Buffer.from(name.replace(/\\/g, '/'), 'utf8');
    const deflated = zlib.deflateRawSync(data, { level: 6 });
    const stored = deflated.length >= data.length;          // incompressible (PNG, MP3, MP4): store as-is
    const body = stored ? data : deflated;
    const crc = zlib.crc32(data) >>> 0;
    const { time, date } = dosDateTime(modified);
    const flags = 0x0800;                                    // UTF-8 file name
    const method = stored ? 0 : 8;
    const localOffset = this.offset;

    const local = Buffer.alloc(30);
    local.writeUInt32LE(0x04034b50, 0);
    local.writeUInt16LE(20, 4);
    local.writeUInt16LE(flags, 6);
    local.writeUInt16LE(method, 8);
    local.writeUInt16LE(time, 10);
    local.writeUInt16LE(date, 12);
    local.writeUInt32LE(crc, 14);
    local.writeUInt32LE(body.length, 18);
    local.writeUInt32LE(data.length, 22);
    local.writeUInt16LE(nameBuf.length, 26);
    local.writeUInt16LE(0, 28);
    this.write(local);
    this.write(nameBuf);
    this.write(body);

    const cen = Buffer.alloc(46);
    cen.writeUInt32LE(0x02014b50, 0);
    cen.writeUInt16LE(20, 4);
    cen.writeUInt16LE(20, 6);
    cen.writeUInt16LE(flags, 8);
    cen.writeUInt16LE(method, 10);
    cen.writeUInt16LE(time, 12);
    cen.writeUInt16LE(date, 14);
    cen.writeUInt32LE(crc, 16);
    cen.writeUInt32LE(body.length, 20);
    cen.writeUInt32LE(data.length, 24);
    cen.writeUInt16LE(nameBuf.length, 28);
    cen.writeUInt32LE(localOffset, 42);
    this.central.push(cen, nameBuf);
    this.count++;
  }

  /** Every file under dir, as "<prefix>/<relative path>"; skip(relPath) leaves a file or folder out.
   *  @param {string} dir  @param {string} prefix  @param {(rel: string, isDir: boolean) => boolean} [skip] */
  addFolder(dir, prefix, skip) {
    /** @param {string} rel */
    const walk = rel => {
      for (const entry of fs.readdirSync(path.join(dir, rel), { withFileTypes: true })) {
        const childRel = rel ? `${rel}/${entry.name}` : entry.name;
        if (skip?.(childRel, entry.isDirectory())) continue;
        const full = path.join(dir, childRel);
        if (entry.isDirectory()) walk(childRel);
        else if (entry.isFile()) this.addFile(prefix ? `${prefix}/${childRel}` : childRel, fs.readFileSync(full), fs.statSync(full).mtime);
      }
    };
    walk('');
  }

  /** @returns {number} the number of files */
  close() {
    const cdStart = this.offset;
    for (const b of this.central) this.write(b);
    const cdSize = this.offset - cdStart;
    const end = Buffer.alloc(22);
    end.writeUInt32LE(0x06054b50, 0);
    end.writeUInt16LE(this.count, 8);
    end.writeUInt16LE(this.count, 10);
    end.writeUInt32LE(cdSize, 12);
    end.writeUInt32LE(cdStart, 16);
    this.write(end);
    fs.closeSync(this.fd);
    return this.count;
  }

  abort() {
    try { fs.closeSync(this.fd); } catch { /* already closed */ }
    try { fs.unlinkSync(this.filePath); } catch { /* nothing written */ }
  }
}
