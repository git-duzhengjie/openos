const fs = require('fs/promises');
const path = require('path');

const MIME_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon'
};

class StaticServer {
  constructor(publicDir) {
    this.publicDir = publicDir;
  }

  async serve(request, response) {
    const requestUrl = new URL(request.url, 'http://localhost');
    const decodedPath = decodeURIComponent(requestUrl.pathname);
    const relativePath = decodedPath === '/' ? 'index.html' : decodedPath.replace(/^\/+/, '');
    const targetPath = path.resolve(this.publicDir, relativePath);

    if (!targetPath.startsWith(path.resolve(this.publicDir))) {
      response.writeHead(403);
      response.end('Forbidden');
      return;
    }

    try {
      const content = await fs.readFile(targetPath);
      response.writeHead(200, {
        'Content-Type': MIME_TYPES[path.extname(targetPath)] || 'application/octet-stream'
      });
      response.end(content);
    } catch (error) {
      if (error.code === 'ENOENT') {
        response.writeHead(404);
        response.end('Not Found');
        return;
      }
      throw error;
    }
  }
}

module.exports = { StaticServer };
