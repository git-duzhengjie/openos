const { createAppId } = require('../../shared/id');

class AppManagementService {
  constructor(options) {
    this.repository = options.repository;
    this.policyService = options.policyService;
  }

  async browse() {
    const apps = await this.repository.list();
    return apps.map((app) => this.toViewModel(app));
  }

  async get(id) {
    const app = await this.repository.findById(id);
    if (!app) {
      throw new Error('应用不存在');
    }
    return this.toViewModel(app);
  }

  async create(payload) {
    this.policyService.assertCreatePayload(payload);

    const now = new Date().toISOString();
    const id = createAppId(payload.name);
    const app = await this.repository.save({
      id,
      name: String(payload.name).trim(),
      version: String(payload.version).trim(),
      description: String(payload.description || '').trim(),
      author: String(payload.author || '').trim(),
      appUrl: String(payload.appUrl).trim(),
      ratingTotal: 0,
      ratingCount: 0,
      createdAt: now,
      updatedAt: now
    });

    return this.toViewModel(app);
  }

  async update(id, payload) {
    this.policyService.assertUpdatePayload(payload);

    const app = await this.repository.findById(id);
    if (!app) {
      throw new Error('应用不存在');
    }

    const updated = await this.repository.save({
      ...app,
      name: payload.name === undefined ? app.name : String(payload.name).trim(),
      version: payload.version === undefined ? app.version : String(payload.version).trim(),
      description: payload.description === undefined ? app.description : String(payload.description).trim(),
      author: payload.author === undefined ? app.author : String(payload.author).trim(),
      appUrl: payload.appUrl === undefined ? app.appUrl : String(payload.appUrl).trim(),
      updatedAt: new Date().toISOString()
    });

    return this.toViewModel(updated);
  }

  async delete(id) {
    const deleted = await this.repository.delete(id);
    if (!deleted) {
      throw new Error('应用不存在');
    }
    return this.toViewModel(deleted);
  }

  async rate(id, score) {
    this.policyService.assertRating(score);

    const app = await this.repository.findById(id);
    if (!app) {
      throw new Error('应用不存在');
    }

    const updated = await this.repository.save({
      ...app,
      ratingTotal: app.ratingTotal + Number(score),
      ratingCount: app.ratingCount + 1,
      updatedAt: new Date().toISOString()
    });

    return this.toViewModel(updated);
  }

  toViewModel(app) {
    const averageRating = app.ratingCount > 0 ? Number((app.ratingTotal / app.ratingCount).toFixed(1)) : 0;
    return {
      id: app.id,
      name: app.name,
      version: app.version,
      description: app.description,
      author: app.author,
      appUrl: app.appUrl,
      ratingCount: app.ratingCount,
      averageRating,
      createdAt: app.createdAt,
      updatedAt: app.updatedAt
    };
  }
}

module.exports = { AppManagementService };
