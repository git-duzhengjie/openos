const http = require('http');
const path = require('path');
const { AppPolicyService } = require('./domain/services/appPolicyService');
const { AppManagementService } = require('./application/services/appManagementService');
const { FileAppRepository } = require('./infrastructure/repositories/fileAppRepository');
const { AppController } = require('./presentation/controllers/appController');
const { Router } = require('./presentation/http/router');
const { StaticServer } = require('./presentation/http/staticServer');
const { sendError } = require('./shared/http');

function createServer(options = {}) {
  const rootDir = options.rootDir || path.resolve(__dirname, '..');
  const dataDir = options.dataDir || path.join(rootDir, 'data');
  const publicDir = options.publicDir || path.join(rootDir, 'public');

  const repository = new FileAppRepository({
    dataFile: options.dataFile || path.join(dataDir, 'apps.json')
  });
  const policyService = new AppPolicyService();
  const appService = new AppManagementService({ repository, policyService });
  const appController = new AppController(appService);
  const staticServer = new StaticServer(publicDir);
  const router = new Router({ appController, staticServer });

  return http.createServer(async (request, response) => {
    try {
      await router.handle(request, response);
    } catch (error) {
      sendError(response, 500, error);
    }
  });
}

function start() {
  const port = Number(process.env.PORT || 7070);
  const host = process.env.HOST || '127.0.0.1';
  const server = createServer();

  server.listen(port, host, () => {
    console.log(`openoshub is running at http://${host}:${port}`);
  });

  return server;
}

if (require.main === module) {
  start();
}

module.exports = { createServer, start };
