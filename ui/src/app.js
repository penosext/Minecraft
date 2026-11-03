import { BasePage } from './base-page.js'
class App extends $falcon.App {
  constructor() {
    super()
  }

  onLaunch(options) {
    super.onLaunch(options)
    $falcon.useDefaultBasePageClass(BasePage)
  }

  onShow() {
    super.onShow()
  }

  onHide() {
    super.onHide()
  }

  onDestroy() {
    super.onDestroy()
  }
}

try {
  globalThis['window'] = {
    requestAnimationFrame,
    cancelAnimationFrame
  }
} catch (err) {
  console.log(err)
}

try {
  globalThis['process'] = {
    env: {
      NODE_ENV: 'production'
    }
  }
} catch (err) {
  console.log(err)
}

export default App
