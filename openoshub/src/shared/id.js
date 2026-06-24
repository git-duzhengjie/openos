const crypto = require('crypto');

function createAppId(name) {
  const normalizedName = String(name || 'app')
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9\u4e00-\u9fa5]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, 40) || 'app';
  const suffix = crypto.randomBytes(4).toString('hex');
  return `app-${normalizedName}-${suffix}`;
}

module.exports = { createAppId };
