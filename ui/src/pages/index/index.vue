<template>
  <div class="container" @touchend="releaseTouches" @touchcancel="releaseTouches">
    <surface ref="sf" :width="960" :height="266" class="game-surface"
      @touchstart="onLookStart" @touchmove="onLookMove"
      @touchend="onLookEnd" @touchcancel="onLookCancel"></surface>

    <div v-if="!inventoryPanelOpen && !worldMode" class="hud" @touchstart="onControlTouch" @touchmove="onControlTouch">
      <text class="hud-text">FPS {{ fpsText }} (提交) · 渲染 {{ renderMsText }} ms · 角度 {{ hudYaw }}° / {{ hudPitch }}°</text>
    </div>
    <!-- Debug overlay: which lines/items appear is chosen in 设置 → 调试信息. -->
    <div v-if="!inventoryPanelOpen && worldMode && hudLines.length" class="hud world-hud"
      :style="{ height: hudHeight + 'px' }" @touchstart="onControlTouch" @touchmove="onControlTouch">
      <text v-for="(line, index) in hudLines" :key="index" class="hud-text world-hud-text">{{ line }}</text>
    </div>
    <div v-if="!worldJobVisible && (actionMessage || nativeResult || (worldMode && (worldInfoError || inventoryError)))" class="render-status"
      :class="{ 'world-status': worldMode, 'inventory-status': inventoryPanelOpen }" @touchstart="onControlTouch" @touchmove="onControlTouch">
      <text class="status-text">{{ actionMessage || nativeResult || worldInfoError || inventoryError }}</text>
    </div>

    <div v-if="!worldMode" class="toolbar" @touchstart="onControlTouch" @touchmove="onControlTouch">
      <div class="toolbar-button" @click="previousBlock">
        <text class="button-text">上一块</text>
      </div>
      <div class="current-block" @click="toggleBlockPicker">
        <text class="current-name">{{ currentBlockName }}</text>
        <text class="current-index">渲染状态预览 · {{ blockPosition }} · 点此选择</text>
      </div>
      <div class="toolbar-button" @click="nextBlock">
        <text class="button-text">下一块</text>
      </div>
      <div class="toolbar-button reset-button" @click="resetView">
        <text class="button-text">重置</text>
      </div>
      <div class="toolbar-button create-button" @click="openCreatePanel">
        <text class="button-text">创建新世界</text>
      </div>
      <div v-if="lastWorldPath" class="toolbar-button continue-button" @click="continueWorld">
        <text class="button-text">继续本次世界</text>
      </div>
    </div>

    <!-- The 1.25× native panel reaches x=590, so both top-right buttons are hidden
         while it is open; world input is blocked then, so no dead zone remains. -->
    <div v-if="worldMode && !inventoryPanelOpen" class="settings-button" @touchstart="onControlTouch" @touchmove="onControlTouch" @click="openWorldSettings">
      <text class="button-text">设置</text>
    </div>
    <div v-if="worldMode && !inventoryPanelOpen" class="bag-button" @touchstart="onControlTouch" @touchmove="onControlTouch" @click="openInventory">
      <text class="button-text">背包</text>
    </div>
    <!-- Opens the full-screen chat; its input row uses the system input method (EnUSPreferred). -->
    <div v-if="worldMode && !inventoryPanelOpen" class="chat-button" @touchstart="onControlTouch" @touchmove="onControlTouch" @click="openChatPanel">
      <text class="button-text">聊天</text>
    </div>
    <!-- Read-only, self-fading local log. No touch handlers at all, so it can
         never swallow a look drag or a control press underneath it. -->
    <div v-if="chatVisible" class="chat-log">
      <text v-for="entry in visibleChat" :key="entry.id" class="chat-line"
        :class="{ 'chat-line-reply': entry.kind === 'reply', 'chat-line-notice': entry.kind === 'notice' }">{{ entry.text }}</text>
    </div>
    <div v-if="worldMode" class="movement-pad" @touchstart="onControlTouch" @touchmove="onControlTouch">
      <div class="move-button move-forward" :class="{ 'control-held': movement.forward }"
        @touchstart="movementStart('forward', $event)" @touchend="movementEnd('forward', $event)"
        @touchcancel="movementEnd('forward', $event)">
        <text class="button-text">前进</text>
      </div>
      <div class="move-button move-left" :class="{ 'control-held': movement.left }"
        @touchstart="movementStart('left', $event)" @touchend="movementEnd('left', $event)"
        @touchcancel="movementEnd('left', $event)">
        <text class="button-text">左移</text>
      </div>
      <div class="move-button move-backward" :class="{ 'control-held': movement.backward }"
        @touchstart="movementStart('backward', $event)" @touchend="movementEnd('backward', $event)"
        @touchcancel="movementEnd('backward', $event)">
        <text class="button-text">后退</text>
      </div>
      <div class="move-button move-right" :class="{ 'control-held': movement.right }"
        @touchstart="movementStart('right', $event)" @touchend="movementEnd('right', $event)"
        @touchcancel="movementEnd('right', $event)">
        <text class="button-text">右移</text>
      </div>
    </div>
    <div v-if="worldMode" class="primary-button" :class="{ 'control-held': primaryHeld }"
      @touchstart="primaryStart" @touchmove="onControlTouch" @touchend="primaryEnd" @touchcancel="primaryEnd">
      <text class="button-text">左键</text>
    </div>
    <div v-if="worldMode" class="secondary-button" :class="{ 'control-held': secondaryHeld }"
      @touchstart="secondaryStart" @touchmove="onControlTouch" @touchend="secondaryEnd" @touchcancel="secondaryEnd">
      <text class="button-text">右键</text>
    </div>
    <div v-if="worldMode" class="jump-button" :class="{ 'control-held': movement.jump }"
      @touchstart="movementStart('jump', $event)" @touchmove="onControlTouch" @touchend="movementEnd('jump', $event)"
      @touchcancel="movementEnd('jump', $event)">
      <text class="button-text">跳跃</text>
    </div>

    <!-- Native 1.25× GUI. Vue provides transparent hits only, never item/count rendering.
         Every offset and size comes from the mirrored native layout in index.ts. -->
    <div v-if="worldMode && !worldInputBlocked" class="hotbar-hit-layer"
      @touchstart="onControlTouch" @touchmove="onControlTouch">
      <div v-for="hit in hotbarHits" :key="hit.slot" class="hotbar-hit"
        :style="{ left: hit.left + 'px', top: hit.top + 'px', width: hit.width + 'px', height: hit.height + 'px' }"
        @click="selectInventory(hit.slot)"></div>
    </div>

    <div v-if="worldMode && inventoryPanelOpen" class="inventory-overlay"
      @touchstart="onControlTouch" @touchmove="onControlTouch">
      <div class="inventory-backdrop" @click="closeInventory"></div>
      <div class="inventory-panel-hit">
        <div v-for="hit in inventoryHits" :key="hit.slot" class="inventory-slot-hit"
          :style="{ left: hit.left + 'px', top: hit.top + 'px', width: hit.width + 'px', height: hit.height + 'px' }"
          @click="clickInventorySlot(hit.slot)"></div>
      </div>
      <div class="inventory-tools">
        <text class="inventory-hint">持物：{{ heldItemName }}</text>
        <text v-if="inventoryMigrationWarning" class="inventory-migration-warning">{{ inventoryMigrationWarning }}</text>
        <div class="inventory-tool-button" @click="toggleBlockPicker">
          <text class="button-text">创造物品</text>
        </div>
        <div class="inventory-tool-button inventory-close" @click="closeInventory">
          <text class="button-text">关闭背包</text>
        </div>
      </div>
    </div>

    <div v-if="pickerOpen" class="block-picker" @touchstart="onControlTouch" @touchmove="onControlTouch">
      <div class="picker-header">
        <text v-if="worldMode" class="picker-title">创造物品 · {{ creativeItems.length }} 项 · 上下滑动选择</text>
        <text v-if="!worldMode" class="picker-title">渲染状态预览 · {{ terrainBlocks.length }} 项 · 上下滑动选择</text>
        <div class="picker-close" @click="closeBlockPicker">
          <text class="button-text">关闭</text>
        </div>
      </div>
      <scroller class="block-scroller" scroll-direction="vertical">
        <div v-if="worldMode" class="block-grid">
          <div v-for="(item, index) in creativeItems" :key="item.key"
            class="block-option" :class="{ 'block-option-selected': item.key === selectedInventoryKey }"
            @click="selectBlock(index)">
            <text class="block-name">{{ item.key === selectedInventoryKey ? '● ' : '' }}{{ item.name }}</text>
            <text class="block-detail">ID {{ item.itemId }} · aux {{ item.aux }}</text>
          </div>
        </div>
        <div v-if="!worldMode" class="block-grid">
          <div v-for="(block, index) in terrainBlocks" :key="block.key"
            class="block-option" :class="{ 'block-option-selected': index === blockIndex }"
            @click="selectBlock(index)">
            <text class="block-name">{{ index === blockIndex ? '● ' : '' }}{{ index + 1 }}. {{ block.name }}</text>
            <text class="block-detail">{{ block.shape }} · {{ block.quality }}</text>
          </div>
        </div>
      </scroller>
    </div>

    <div v-if="createPanelOpen && !worldMode" class="create-panel" @touchstart="onControlTouch" @touchmove="onControlTouch">
      <div class="create-header">
        <text class="picker-title">创建临时世界</text>
        <div class="picker-close" @click="closeCreatePanel">
          <text class="button-text">关闭</text>
        </div>
      </div>
      <text class="create-hint">/tmp/mc/* · 设备重启后丢失 · 非原版存档，仅可继续本次世界</text>
      <text class="generator-hint">多群系 · 建筑依原规则 · 昼夜 20 分钟 · 轻量 AI · 无洞穴</text>
      <text class="scene-hint">只加载玩家附近 · 进入新区块才排队 · 跨界可能短暂停顿</text>
      <div class="create-radius-row">
        <text class="size-label">渲染距离</text>
        <div v-for="option in renderRadiusOptions" :key="option.radius" class="radius-option"
          :class="{ 'radius-selected': selectedRenderRadius === option.radius }"
          @click="selectRenderRadius(option.radius)">
          <text class="size-option-text">{{ option.label }}</text>
        </div>
      </div>
      <div class="world-size-row">
        <text class="size-label">世界尺寸</text>
        <div v-for="option in worldSizeOptions" :key="option.size" class="world-size-option"
          :class="{ 'world-size-selected': selectedWorldSize === option.size }"
          @click="selectWorldSize(option.size)">
          <text class="size-option-text">{{ option.label }} {{ option.size }}</text>
        </div>
      </div>
      <text class="size-hint">{{ selectedWorldSize }}×{{ selectedWorldSize }} · 高度 256 · 实际加载以原生队列为准</text>
      <!-- Tapping the field opens the system numeric keypad; the native input
           event stays wired for hosts that have a real keyboard instead. -->
      <div class="seed-row">
        <text class="seed-label">种子（可选）</text>
        <div class="seed-input-hit" @click="openSeedKeyboard">
          <input class="seed-input" type="text" :value="seedInput" @input="onSeedInput"
            @click="openSeedKeyboard" placeholder="留空使用当前时间，无需输入即可创建" />
        </div>
      </div>
      <text class="create-error">{{ actionMessage }}</text>
      <div class="create-actions">
        <div class="panel-create-button" @click="createNewWorld">
          <text class="button-text">{{ worldBusy ? '创建中…' : '创建新世界' }}</text>
        </div>
        <div v-if="lastWorldPath" class="panel-continue-button" @click="continueWorld">
          <text class="button-text">继续本次世界</text>
        </div>
      </div>
    </div>

    <!-- Full-screen settings. Every control is a plain tap target. -->
    <div v-if="worldMode && settingsPanelOpen" class="settings-screen"
      @touchstart="onControlTouch" @touchmove="onControlTouch">
      <div class="screen-header">
        <text class="screen-title">设置</text>
        <div v-for="(tab, index) in settingsTabs" :key="tab.key" class="settings-tab"
          :class="{ 'settings-tab-selected': settingsTab === tab.key }" :style="{ left: (90 + index * 112) + 'px' }"
          @click="selectSettingsTab(tab.key)">
          <text class="button-text">{{ tab.label }}</text>
        </div>
        <div class="settings-save-exit" @click="saveAndExit">
          <text class="button-text">{{ saveExitPending ? '等待任务…' : worldBusy ? '保存中…' : '保存退出' }}</text>
        </div>
        <div class="screen-close" @click="closeWorldSettings"><text class="button-text">关闭</text></div>
      </div>

      <div v-if="settingsTab === 'chunks'" class="settings-body">
        <div v-for="range in settingsRanges" :key="range.kind" class="settings-range-row"
          :class="{ 'settings-range-preload': range.kind === 'preload' }">
          <text class="settings-range-label">{{ range.label }}</text>
          <div class="chunk-chip-row">
            <div v-for="stop in chunkRangeStops" :key="stop" class="chunk-chip"
              :class="{ 'chunk-chip-selected': stop === range.radius,
                'chunk-chip-disabled': range.kind === 'preload' && stop < settingsRenderRadius }"
              @click="selectSettingsRange(range.kind, stop)">
              <text class="chunk-chip-text">{{ stop }}</text>
            </div>
          </div>
        </div>
        <text class="settings-stream">{{ streamDetails }}</text>
        <text class="settings-hint">{{ chunkLoadError || '点数字选择 · 应用后生效 · 预加载不小于显示 · 实际档位受设备内存限制' }}</text>
        <text class="settings-error">{{ settingsError }}</text>
        <div class="settings-apply" @click="applyChunkRanges">
          <text class="button-text">应用</text>
        </div>
      </div>

      <div v-if="settingsTab === 'game'" class="settings-body">
        <text class="settings-section-label settings-mode-label">游戏模式（当前：{{ gameModeLabel }}）</text>
        <div class="settings-choice settings-mode-creative"
          :class="{ 'settings-choice-selected': inventoryState && inventoryState.gameMode === 'creative' }"
          @click="setGameModeFromSettings('creative')">
          <text class="button-text">创造</text>
        </div>
        <div class="settings-choice settings-mode-survival"
          :class="{ 'settings-choice-selected': inventoryState && inventoryState.gameMode === 'survival' }"
          @click="setGameModeFromSettings('survival')">
          <text class="button-text">生存</text>
        </div>
        <text class="settings-section-label settings-keep-label">死亡不掉落（KeepInventory）</text>
        <div class="settings-choice settings-keep-toggle" :class="{ 'settings-choice-selected': keepInventoryOn === true }"
          @click="toggleKeepInventory">
          <text class="button-text">{{ keepInventoryOn === null ? '--' : keepInventoryOn ? '已开启' : '已关闭' }}</text>
        </div>
        <text class="settings-keep-note">{{ keepInventoryNote }}</text>
        <text class="settings-game-error">{{ gameSettingsError }}</text>
      </div>

      <div v-if="settingsTab === 'hud'" class="settings-body">
        <text class="settings-section-label">勾选左上角调试信息要显示的内容</text>
        <div class="hud-option-grid">
          <div v-for="item in hudItems" :key="item.key" class="hud-option" @click="toggleHudItem(item.key)">
            <div class="hud-check" :class="{ 'hud-check-on': hudOptions[item.key] }"></div>
            <text class="hud-option-text">{{ item.label }}</text>
          </div>
        </div>
        <div class="settings-choice hud-all-on" @click="setAllHudItems(true)"><text class="button-text">全部显示</text></div>
        <div class="settings-choice hud-all-off" @click="setAllHudItems(false)"><text class="button-text">全部隐藏</text></div>
      </div>
    </div>

    <!-- Full-screen chat: newest at the bottom, paged with 上翻/下翻. -->
    <div v-if="worldMode && chatPanelOpen" class="chat-screen"
      @touchstart="onControlTouch" @touchmove="onControlTouch">
      <div class="screen-header">
        <text class="screen-title">聊天 · 指令输出</text>
        <div class="screen-close" @click="closeChatPanel"><text class="button-text">关闭</text></div>
      </div>
      <div class="chat-history">
        <text v-if="!chatPageLines.length" class="chat-history-empty">暂无消息 · 以 / 开头为指令，输入 /help 查看全部</text>
        <text v-for="entry in chatPageLines" :key="entry.id" class="chat-history-line"
          :class="{ 'chat-history-reply': entry.kind === 'reply', 'chat-history-notice': entry.kind === 'notice' }">{{ entry.text }}</text>
      </div>
      <div class="chat-input" @click="openChatKeyboard">
        <text class="chat-input-text">{{ textEditing ? '正在输入…' : '点此输入聊天或 / 指令' }}</text>
      </div>
      <div v-for="(command, index) in quickCommands" :key="command" class="chat-quick"
        :style="{ left: (500 + index * 92) + 'px' }" @click="runQuickCommand(command)">
        <text class="button-text">{{ command }}</text>
      </div>
      <div class="chat-page chat-page-up" @click="pageChat(1)"><text class="button-text">上翻</text></div>
      <div class="chat-page chat-page-down" @click="pageChat(-1)"><text class="button-text">下翻</text></div>
    </div>

    <div v-if="worldJobVisible" class="world-modal-mask job-mask"
      @touchstart="onControlTouch" @touchmove="onControlTouch">
      <div class="world-job-panel">
        <text class="job-title">{{ worldJobCreated ? '创建世界' : '继续世界' }} · {{ worldJobStage }}</text>
        <text class="job-detail">{{ worldJob ? worldJob.progress + '%' + ' · 区块 ' + worldJob.loadedChunks + '/' + worldJob.totalChunks : '等候原生任务状态…' }}</text>
        <div class="job-progress-track">
          <div v-if="worldJob" class="job-progress-fill" :style="{ width: (worldJob.progress * 4) + 'px' }"></div>
        </div>
        <text class="job-message">{{ worldJobError || actionMessage || '分块生成、加载和保存由原生任务执行' }}</text>
        <div class="job-cancel" @click="cancelWorldJob">
          <text class="button-text">{{ worldJobCancelRequested ? '正在取消…' : '取消' }}</text>
        </div>
      </div>
    </div>
  </div>
</template>

<style lang="less" scoped>
@import url('index.less');
</style>

<script>
import index from './index';
export default index;
</script>
