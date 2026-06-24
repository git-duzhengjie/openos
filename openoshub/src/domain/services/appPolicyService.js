class AppPolicyService {
  assertCreatePayload(payload) {
    const name = String(payload.name || '').trim();
    const version = String(payload.version || '').trim();
    const appUrl = String(payload.appUrl || '').trim();

    if (!name) {
      throw new Error('应用名称不能为空');
    }

    if (!version) {
      throw new Error('应用版本不能为空');
    }

    this.assertAppUrl(appUrl);
  }

  assertUpdatePayload(payload) {
    if (payload.name !== undefined && !String(payload.name).trim()) {
      throw new Error('应用名称不能为空');
    }

    if (payload.version !== undefined && !String(payload.version).trim()) {
      throw new Error('应用版本不能为空');
    }

    if (payload.appUrl !== undefined) {
      this.assertAppUrl(String(payload.appUrl).trim());
    }
  }

  assertAppUrl(appUrl) {
    if (!appUrl) {
      throw new Error('应用 URL 不能为空');
    }

    let parsed;
    try {
      parsed = new URL(appUrl);
    } catch (_error) {
      throw new Error('应用 URL 格式不正确');
    }

    if (!['http:', 'https:'].includes(parsed.protocol)) {
      throw new Error('应用 URL 仅支持 http 或 https');
    }
  }

  assertRating(score) {
    const value = Number(score);
    if (!Number.isInteger(value) || value < 1 || value > 5) {
      throw new Error('评分必须是 1 到 5 的整数');
    }
  }
}

module.exports = { AppPolicyService };
