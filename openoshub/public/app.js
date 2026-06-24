const state = {
  apps: []
};

const elements = {
  form: document.querySelector('#app-form'),
  appId: document.querySelector('#app-id'),
  name: document.querySelector('#name'),
  version: document.querySelector('#version'),
  author: document.querySelector('#author'),
  appUrl: document.querySelector('#appUrl'),
  description: document.querySelector('#description'),
  appList: document.querySelector('#app-list'),
  message: document.querySelector('#message'),
  refresh: document.querySelector('#refresh'),
  formTitle: document.querySelector('#form-title'),
  submitButton: document.querySelector('#submit-button'),
  cancelEdit: document.querySelector('#cancel-edit')
};

function showMessage(text, type = 'info') {
  elements.message.textContent = text;
  elements.message.className = `message ${type}`;
  window.clearTimeout(showMessage.timer);
  showMessage.timer = window.setTimeout(() => {
    elements.message.className = 'message hidden';
  }, 3200);
}

async function requestJson(url, options = {}) {
  const response = await fetch(url, {
    ...options,
    headers: {
      'Content-Type': 'application/json',
      ...(options.headers || {})
    }
  });
  const data = await response.json();
  if (!response.ok || !data.success) {
    throw new Error(data.error || '请求失败');
  }
  return data.data;
}

function readForm() {
  return {
    name: elements.name.value.trim(),
    version: elements.version.value.trim(),
    author: elements.author.value.trim(),
    appUrl: elements.appUrl.value.trim(),
    description: elements.description.value.trim()
  };
}

function resetForm() {
  elements.form.reset();
  elements.appId.value = '';
  elements.formTitle.textContent = '登记应用 URL';
  elements.submitButton.textContent = '登记应用';
  elements.cancelEdit.classList.add('hidden');
}

async function loadApps() {
  state.apps = await requestJson('/api/apps');
  renderApps();
}

function renderApps() {
  if (state.apps.length === 0) {
    elements.appList.innerHTML = '<p class="empty">还没有应用，先登记一个应用 URL 吧。</p>';
    return;
  }

  elements.appList.innerHTML = state.apps.map((app) => `
    <article class="app-card">
      <div class="app-main">
        <div>
          <h3>${escapeHtml(app.name)}</h3>
          <p class="meta">版本 ${escapeHtml(app.version)} · ${escapeHtml(app.author || '未知作者')}</p>
        </div>
        <div class="rating">★ ${app.averageRating || 0} <span>(${app.ratingCount})</span></div>
      </div>
      <p>${escapeHtml(app.description || '暂无描述')}</p>
      <a class="app-url" href="${escapeAttribute(app.appUrl)}" target="_blank" rel="noopener noreferrer">${escapeHtml(app.appUrl)}</a>
      <div class="card-actions">
        <button data-action="open" data-id="${escapeAttribute(app.id)}">打开 URL</button>
        <button data-action="edit" data-id="${escapeAttribute(app.id)}" class="secondary">修改</button>
        <button data-action="delete" data-id="${escapeAttribute(app.id)}" class="danger">删除</button>
      </div>
      <div class="score-row">
        <span>评分：</span>
        ${[1, 2, 3, 4, 5].map((score) => `<button class="score" data-action="rate" data-id="${escapeAttribute(app.id)}" data-score="${score}">${score}</button>`).join('')}
      </div>
    </article>
  `).join('');
}

function editApp(id) {
  const app = state.apps.find((item) => item.id === id);
  if (!app) {
    showMessage('应用不存在', 'error');
    return;
  }

  elements.appId.value = app.id;
  elements.name.value = app.name;
  elements.version.value = app.version;
  elements.author.value = app.author || '';
  elements.appUrl.value = app.appUrl;
  elements.description.value = app.description || '';
  elements.formTitle.textContent = '修改应用';
  elements.submitButton.textContent = '保存修改';
  elements.cancelEdit.classList.remove('hidden');
  window.scrollTo({ top: 0, behavior: 'smooth' });
}

async function deleteApp(id) {
  if (!window.confirm('确定要删除这个应用吗？')) {
    return;
  }

  await requestJson(`/api/apps/${encodeURIComponent(id)}`, { method: 'DELETE' });
  showMessage('应用已删除', 'success');
  await loadApps();
}

async function rateApp(id, score) {
  await requestJson(`/api/apps/${encodeURIComponent(id)}/ratings`, {
    method: 'POST',
    body: JSON.stringify({ score })
  });
  showMessage('评分成功', 'success');
  await loadApps();
}

function openAppUrl(id) {
  const app = state.apps.find((item) => item.id === id);
  if (app) {
    window.open(app.appUrl, '_blank', 'noopener,noreferrer');
  }
}

elements.form.addEventListener('submit', async (event) => {
  event.preventDefault();
  const id = elements.appId.value;
  const payload = readForm();

  try {
    if (id) {
      await requestJson(`/api/apps/${encodeURIComponent(id)}`, {
        method: 'PUT',
        body: JSON.stringify(payload)
      });
      showMessage('应用已修改', 'success');
    } else {
      await requestJson('/api/apps', {
        method: 'POST',
        body: JSON.stringify(payload)
      });
      showMessage('应用已登记', 'success');
    }
    resetForm();
    await loadApps();
  } catch (error) {
    showMessage(error.message, 'error');
  }
});

elements.appList.addEventListener('click', async (event) => {
  const button = event.target.closest('button[data-action]');
  if (!button) {
    return;
  }

  const action = button.dataset.action;
  const id = button.dataset.id;

  try {
    if (action === 'open') {
      openAppUrl(id);
    } else if (action === 'edit') {
      editApp(id);
    } else if (action === 'delete') {
      await deleteApp(id);
    } else if (action === 'rate') {
      await rateApp(id, Number(button.dataset.score));
    }
  } catch (error) {
    showMessage(error.message, 'error');
  }
});

elements.refresh.addEventListener('click', async () => {
  try {
    await loadApps();
    showMessage('已刷新', 'success');
  } catch (error) {
    showMessage(error.message, 'error');
  }
});

elements.cancelEdit.addEventListener('click', resetForm);

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function escapeAttribute(value) {
  return escapeHtml(value).replace(/`/g, '&#96;');
}

loadApps().catch((error) => showMessage(error.message, 'error'));
