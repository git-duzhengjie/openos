const { sendError } = require('../../shared/http');

class Router {
  constructor(options) {
    this.appController = options.appController;
    this.staticServer = options.staticServer;
  }

  async handle(request, response) {
    const requestUrl = new URL(request.url, 'http://localhost');
    const pathname = requestUrl.pathname;
    const method = request.method || 'GET';

    if (method === 'GET' && pathname === '/api/apps') {
      return this.appController.handle('list', request, response);
    }

    if (method === 'POST' && pathname === '/api/apps') {
      return this.appController.handle('create', request, response);
    }

    const appMatch = pathname.match(/^\/api\/apps\/([^/]+)$/);
    if (appMatch && method === 'GET') {
      return this.appController.handle('get', request, response, { id: decodeURIComponent(appMatch[1]) });
    }

    if (appMatch && method === 'PUT') {
      return this.appController.handle('update', request, response, { id: decodeURIComponent(appMatch[1]) });
    }

    if (appMatch && method === 'DELETE') {
      return this.appController.handle('delete', request, response, { id: decodeURIComponent(appMatch[1]) });
    }

    const rateMatch = pathname.match(/^\/api\/apps\/([^/]+)\/ratings$/);
    if (rateMatch && method === 'POST') {
      return this.appController.handle('rate', request, response, { id: decodeURIComponent(rateMatch[1]) });
    }

    if (pathname.startsWith('/api/')) {
      sendError(response, 404, '接口不存在');
      return undefined;
    }

    return this.staticServer.serve(request, response);
  }
}

module.exports = { Router };
