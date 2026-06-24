function sendJson(response, statusCode, payload) {
  response.writeHead(statusCode, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store'
  });
  response.end(JSON.stringify(payload));
}

function sendError(response, statusCode, error) {
  sendJson(response, statusCode, {
    success: false,
    error: error instanceof Error ? error.message : String(error)
  });
}

function collectBody(request, limit = 1024 * 1024) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;

    request.on('data', (chunk) => {
      size += chunk.length;
      if (size > limit) {
        reject(new Error('请求体过大'));
        request.destroy();
        return;
      }
      chunks.push(chunk);
    });

    request.on('end', () => resolve(Buffer.concat(chunks)));
    request.on('error', reject);
  });
}

async function readJson(request) {
  const body = await collectBody(request);
  if (!body.length) {
    return {};
  }
  return JSON.parse(body.toString('utf8'));
}

module.exports = { sendJson, sendError, collectBody, readJson };
