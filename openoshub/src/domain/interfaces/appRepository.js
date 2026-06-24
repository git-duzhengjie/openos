class IAppRepository {
  async list() {
    throw new Error('IAppRepository.list must be implemented');
  }

  async findById(_id) {
    throw new Error('IAppRepository.findById must be implemented');
  }

  async save(_app) {
    throw new Error('IAppRepository.save must be implemented');
  }

  async delete(_id) {
    throw new Error('IAppRepository.delete must be implemented');
  }
}

module.exports = { IAppRepository };
