const { readJson, sendError, sendJson } = require('../../shared/http');

class AppController {
  constructor(appService) {
    this.appService = appService;
  }

  async list(_request, response) {
    const apps = await this.appService.browse();
    sendJson(response, 200, { success: true, data: apps });
  }

  async get(_request, response, params) {
    const app = await this.appService.get(params.id);
    sendJson(response, 200, { success: true, data: app });
  }

  async create(request, response) {
    const payload = await readJson(request);
    const app = await this.appService.create(payload);
    sendJson(response, 201, { success: true, data: app });
  }

  async update(request, response, params) {
    const payload = await readJson(request);
    const app = await this.appService.update(params.id, payload);
    sendJson(response, 200, { success: true, data: app });
  }

  async delete(_request, response, params) {
    const app = await this.appService.delete(params.id);
    sendJson(response, 200, { success: true, data: app });
  }

  async rate(request, response, params) {
    const payload = await readJson(request);
    const app = await this.appService.rate(params.id, payload.score);
    sendJson(response, 200, { success: true, data: app });
  }

  async handle(action, request, response, params = {}) {
    try {
      await this[action](request, response, params);
    } catch (error) {
      const status = error.message === '应用不存在' ? 404 : 400;
      sendError(response, status, error);
    }
  }
}

module.exports = { AppController };
