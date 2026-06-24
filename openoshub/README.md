# openoshub

openoshub 是 OPENOS 的独立 Node.js 应用中心，用于管理用户自定义应用。

当前版本不再上传应用安装包，而是由用户提供应用 URL。openoshub 负责登记应用元数据，并提供浏览、修改、删除和评分能力。

## 功能

- 登记应用 URL
- 浏览应用列表
- 修改应用信息
- 删除应用记录
- 给应用评分
- 打开应用 URL

## 数据字段

每个应用记录包含：

```json
{
  "id": "app-demo-xxxxxxxx",
  "name": "示例应用",
  "version": "1.0.0",
  "description": "示例描述",
  "author": "OPENOS",
  "appUrl": "https://example.com/demo.openosapp",
  "ratingTotal": 0,
  "ratingCount": 0,
  "createdAt": "2026-06-24T00:00:00.000Z",
  "updatedAt": "2026-06-24T00:00:00.000Z"
}
```

## 启动

```bash
cd E:/openos/openoshub
npm start
```

默认地址：

```text
http://127.0.0.1:7070
```

可通过环境变量修改监听地址：

```bash
HOST=0.0.0.0 PORT=7070 npm start
```

## API

### 浏览应用

```http
GET /api/apps
```

### 查看应用详情

```http
GET /api/apps/:id
```

### 登记应用 URL

```http
POST /api/apps
Content-Type: application/json
```

请求体：

```json
{
  "name": "示例应用",
  "version": "1.0.0",
  "author": "OPENOS",
  "appUrl": "https://example.com/demo.openosapp",
  "description": "这是一个示例应用"
}
```

说明：

- `name` 必填
- `version` 必填
- `appUrl` 必填
- `appUrl` 仅支持 `http` 或 `https`

### 修改应用

```http
PUT /api/apps/:id
Content-Type: application/json
```

请求体支持局部更新：

```json
{
  "name": "新应用名称",
  "version": "1.1.0",
  "appUrl": "https://example.com/demo-1.1.0.openosapp",
  "description": "新版描述"
}
```

### 删除应用

```http
DELETE /api/apps/:id
```

### 应用评分

```http
POST /api/apps/:id/ratings
Content-Type: application/json
```

请求体：

```json
{
  "score": 5
}
```

`score` 必须是 1 到 5 的整数。

## 数据存储

应用数据保存在：

```text
openoshub/data/apps.json
```

## 语法检查

```bash
npm run check
```
