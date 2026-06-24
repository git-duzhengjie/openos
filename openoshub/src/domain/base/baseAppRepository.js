const { IAppRepository } = require('../interfaces/appRepository');

class BaseAppRepository extends IAppRepository {
  normalizeApp(app) {
    return {
      id: String(app.id || ''),
      name: String(app.name || ''),
      version: String(app.version || '1.0.0'),
      description: String(app.description || ''),
      author: String(app.author || ''),
      appUrl: String(app.appUrl || ''),
      ratingTotal: Number(app.ratingTotal || 0),
      ratingCount: Number(app.ratingCount || 0),
      createdAt: app.createdAt || new Date().toISOString(),
      updatedAt: app.updatedAt || new Date().toISOString()
    };
  }
}

module.exports = { BaseAppRepository };
