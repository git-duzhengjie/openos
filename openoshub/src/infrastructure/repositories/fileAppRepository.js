const fs = require('fs/promises');
const path = require('path');
const { BaseAppRepository } = require('../../domain/base/baseAppRepository');

class FileAppRepository extends BaseAppRepository {
  constructor(options) {
    super();
    this.dataFile = options.dataFile;
  }

  async ensureStore() {
    await fs.mkdir(path.dirname(this.dataFile), { recursive: true });

    try {
      await fs.access(this.dataFile);
    } catch (_error) {
      await fs.writeFile(this.dataFile, '[]\n', 'utf8');
    }
  }

  async list() {
    await this.ensureStore();
    const content = await fs.readFile(this.dataFile, 'utf8');
    const apps = JSON.parse(content || '[]');
    return apps.map((app) => this.normalizeApp(app));
  }

  async findById(id) {
    const apps = await this.list();
    return apps.find((app) => app.id === id) || null;
  }

  async save(app) {
    await this.ensureStore();
    const normalized = this.normalizeApp(app);
    const apps = await this.list();
    const index = apps.findIndex((item) => item.id === normalized.id);

    if (index >= 0) {
      apps[index] = normalized;
    } else {
      apps.push(normalized);
    }

    await this.writeAll(apps);
    return normalized;
  }

  async delete(id) {
    await this.ensureStore();
    const apps = await this.list();
    const target = apps.find((item) => item.id === id);

    if (!target) {
      return null;
    }

    await this.writeAll(apps.filter((item) => item.id !== id));
    return target;
  }

  async writeAll(apps) {
    const sorted = [...apps].sort((a, b) => a.name.localeCompare(b.name, 'zh-Hans-CN'));
    await fs.writeFile(this.dataFile, `${JSON.stringify(sorted, null, 2)}\n`, 'utf8');
  }
}

module.exports = { FileAppRepository };
