# Prepare 1.0.14: accelerate NetCDF/HDF5 expressions and verify standalone release packaging

大型多時間表達式原本會依序讀取與計算，無法充分使用可用 CPU；反覆平均、積分及剖面運算也可能重複讀取或解壓相同資料。本 PR 將符合條件的工作交給有記憶體上限的批次 reader、OpenMP 歸約與隔離時間 workers，保留主程序的結果順序、缺值規則、DEFINE 狀態、進度、取消與圖形控制。並補齊 Linux 開發環境、獨立 release 打包驗證與 tmux 相容處理。

這份 PR 準備 **opengrads-hpc 1.0.14**；GrADS 基底維持 **2.2.1.oga.1**。

## 合併目標與版本範圍

| 項目 | 設定 |
| --- | --- |
| 既有 PR | https://github.com/Lance798/opengrads-hpc/pull/4 |
| Base repository / branch | `Lance798/opengrads-hpc:main` |
| Base commit | `0b076ad0b34b4e89eba87fa5218d27f4a09138ef`，1.0.13 |
| Head repository / branch | `tsengshao/opengrads-hpc:feat/Accelerate` |
| Distribution version | `release/VERSION = 1.0.14` |
| GrADS baseline | `cola/src/VERSION = 2.2.1.oga.1` |

本 PR 的所有新增加速、NetCDF／靜態 DEFINE 擴充、開發／CI／打包與終端修正都歸入 1.0.14。`zslice`、`zint`、`esat`、`qvsat`、`clear canvas` 與變數底線命名已在 1.0.13 存在；這次主要擴充其讀取與計算加速，不將既有函數重新列為 1.0.14 新增。

## 1. Reader 與跨格式歸約

- BP5／HDF5 垂直平均以水平 tile 批次讀取多層資料，減少逐層重複解壓。
- 共用完整 X/Z、Y/Z 剖面與座標權重，避免 DEFINE 的前幾個位置被額外讀取。
- 記憶體內 defined arrays 使用私有唯讀視圖，減少中間複製；以有界的列與區段進行 SIMD／OpenMP 歸約。
- BP5、HDF5、NetCDF、binary 的長時間平均與面積平均時間序列使用有記憶體上限的批次路徑；`ave(a+b)` 等符合條件的組合保留逐時間的共同有效遮罩。
- 非週期邊界保留原輸出範圍與 halo，只讀取檔案內有效交集；binary 將邊界資料整合為有界連續 slab。
- 嚴格同欄位門檻，例如 `maskout(f,(f>cut)-0.5)`，可重用已讀取的 `f`，避免讀取第二次；`>`／`>=` 的門檻語意保持各自原有規則。
- 修正 HDF5 scalar／單元素屬性、packed 值、surface/profile 廣播，以及剩餘 T/E 結果軸誤用空間座標 callback 的問題。

## 2. NetCDF/HDF5 隔離時間 workers

### Output-time expressions

持續存在的無圖形 GrADS 子程序各使用一條計算執行緒，在不同時間執行完整既有 evaluator。主程序組裝 DEFINE 或直接 T、Z/T 剖面 DISPLAY 的結果，並集中管理圖形與 session 狀態。

支援預設 NetCDF/HDF5 descriptor 的原生欄位、四則運算、比較／邏輯、冪次、三角函數、遮罩、飽和診斷及既有外層空間／垂直歸約剖面。符合條件的後續命令重用 worker pool；CPU 預算變更、close/reset/reinit/quit、取消及失敗會清理或重建相關狀態。

### Input-time expressions

`sum(expr,t=...,t=...)` 即使只輸出一張地圖，也能平行計算各輸入時間的完整 `expr`。同一 producer 服務 `ave`、`mean`、`sum`、`sumg`、`min`、`max`、`minloc`、`maxloc`。

主程序保留初始化、依時間先後累加、分數權重、缺值分母與極值同值位置規則。它接收各時間的網格後按原順序歸約，不以 worker 局部平均取代原本公式。

### 函數範圍

目前完整 worker 表達式清單包含 26 個內建名稱：

| 類別 | 函數 |
| --- | --- |
| 數學 | `abs sqrt exp log log10 pow mag` |
| 三角函數 | `sin cos tan asin acos atan2` |
| 遮罩／值處理 | `maskout const` |
| 飽和診斷 | `esat qvsat` |
| 平均／加總 | `ave mean sum sumg aave amean asum atot` |
| 高度積分 | `zint` |

外層 MIN/MAX/MINLOC/MAXLOC 可使用 input-time producer，但仍不屬於可任意巢狀使用的完整表達式清單。每個命令還需符合資料格式、維度與工作量條件；完整規則見 [TIME_WORKERS.md](https://github.com/tsengshao/opengrads-hpc/blob/feat/Accelerate/docs/TIME_WORKERS.md)。

### NetCDF 與靜態 DEFINE 擴充

- classic NetCDF 與壓縮 NetCDF-4 共用 HDF5 的排程、語法、上限與復原規則；各 worker 自行開檔，保留 OPEN/SDFOPEN/XDFOPEN 方法與 template 時間選檔。
- Native surface/profile 從實際存在的 X/Y/Z/T 軸廣播，保留 unpacking 與 missing masks。
- T/E 長度皆為一的已存 scalar、Z profile、XY map、XYZ cube，可以搭配預設檔案的原生欄位送入 workers。
- 每個命令重新擷取引用的定義值、遮罩、offset 與座標；重複名稱只傳一次。Re-DEFINE、UNDEFINE、SET DEFVAL 的變更會反映在下一個命令，即使 PIDs 被重用。
- 定義值直接傳輸，不重開其原始來源、不重跑原公式。因此從第二個 binary 檔案保存的密度剖面，可在關閉來源檔案後搭配 NetCDF/HDF5 的 QV 使用。
- 靜態輸入合計上限 128 MiB、32 個不同定義，各空間／非線性座標軸上限 65,536 個元素；超限或不支援的定義沿用既有 evaluator。

## 3. HDF5 ZINT 與資源上限

HDF5 密集 chunk 讀取使用與原生 rank/extent 相符的 memory dataspace；簡單乘積中的密度因子可一次讀成垂直向量。HDF5 2.0+ 且符合原生 float/double、chunk layout 與 shuffle/deflate/Fletcher32 組合時，主執行緒讀取 raw chunks，OpenMP 執行緒以私有工作區解碼，再保留 chunk／垂直順序歸約。Checksum 持續驗證。

`set threads N`、`-j N` 或 `GA_NUM_THREADS=N` 控制總計算 CPU 預算，預設四。主要限制：

- 時間 workers 最多 128 個，並受時間數量、batch 長度及可用記憶體估計限制。
- 一般 output-time 完整表達式至少需要 32 個時間；DEFINE 另有最低工作量條件。
- 單一時間結果最多 128 MiB；worker 工作區估計受 8 GiB 與可用記憶體四分之一的較小值限制，並計入靜態 DEFINE 輸入。
- 單時間解壓每批最多 128 chunks，raw/scratch/選值工作區上限 256 MiB，metadata 另限 16 MiB。
- Linux 使用 `MemAvailable` 納入可回收快取。這些限制是工作區預算；完整 DEFINE 輸出仍另行配置，並非整個程序的硬性 RSS 上限。
- BP5 保留既有 reader／OpenMP 路徑；未啟用同一套完整時間表達式 workers。

## 4. 開發、CI 與完整 release

新增 Linux `dev/activate.sh`、`dev/run`、`dev/build.sh`、`dev/test.sh`、`dev/package.sh`，在明確的主機工具鏈與私有依賴前綴下執行，不依賴 Conda。啟用與停用會保存／還原環境；HDF5 pkg-config 與 Python 版本處理涵蓋 RHEL 8。

開發 CI 在 Ubuntu 22.04 與 manylinux_2_28/RHEL 8 家族容器內執行：

1. 安裝各平台主機建置前置套件。
2. 由 checksum-pinned 來源建立私有依賴與 GrADS。
3. 執行開發回歸，包含 HDF5、classic NC、壓縮 NC4 與靜態 DEFINE。
4. 建立完整 `.tar.gz`、`.sha256`，執行封裝內的數值、graphics 與 bundled-loader 檢查。
5. 解壓到另一個目錄，以 `env -i` 啟動 archive，並用 Term file mode 產生 PNG。
6. 保存 build/test/package/config 診斷。Binary artifact upload 保留既有 `BINARY_REDISTRIBUTION_APPROVED` gate；完整建置、驗證與打包不受該 upload gate 限制。

Linux packager 保留 RHEL ELF LOAD alignment，避免 patchelf 新 segment 與原始 2 MiB alignment 不一致；函式庫檢查失敗會停止打包。Private NetCDF/UDUNITS/JPEG notices、UDUNITS XML、圖形外掛、runtime libraries、版本 helper 與 terminal viewer 一併封裝。封裝後的 ldd／loader 檢查拒絕缺少或逃出 archive 的非 glibc 函式庫依賴。

一般與 forced bundled-loader 路徑均測試時間 workers，修正舊 glibc 主機上將 loader 當成 GrADS executable 的重啟問題。`--version` 與啟動 banner 由 source/archive metadata 決定，不需載入 GrADS runtime。

## 5. tmux 與 Term

圖片 pane 明確以 `TMUX_PANE` 為建立目標，避免使用者切換視窗時圖片跑到別處；保留舊版 tmux 的百分比參數 fallback。

開發環境預設保留啟用前選到的 tmux executable，避免 3.5a client 連到既有 2.7 server。私有 3.5a 由 `TEST_TMUX` 提供給隔離測試，也可明確選用；release 不附帶或強制更換使用者的 tmux。

```bash
# 從專案根目錄、tmux 外啟動獨立的 dev server
OPENGRADS_HOST_TMUX="$PWD/.release-work/deps/bin/tmux" \
  ./dev/run tmux -L opengrads-dev new-session

# 舊 tmux／SSH 沒有提供 iTerm2 身分時
GA_TERM_PROTOCOL=iterm2 ./dev/run ./opengrads -l -d Term
```

`GA_TERM_PROTOCOL` 維持預設 auto，明確設定只覆寫圖片協定。Inline 圖片的單元測試現在自行指定模擬的 iTerm2 環境，並清理測試遺留的 tmux server。

## 6. 效能證據：各階段保留各自基準

以下整理使用者提供的 `tmp/updated` 報告與 JSON。本次沒有重新執行這些大資料 scaling sweeps；數字代表各自指定硬體、資料、cache 與計時邊界。**不同表格的倍數不能相乘，也不能全部當作 1.0.14 對 1.0.13 的版本加速比。**

### A. Derecho：main → 6cc0735，同執行緒數前後比較

暖快取、三次中位數，包含讀取與計算，排除啟動、開檔、輸出與繪圖；測試使用 GCC 14.3、CPU 0–7。

| 工作 | CPU | 修改前 → 修改後 | 加速 |
| --- | ---: | ---: | ---: |
| BP5 降水平均，1024²、144 時次 | 4 | 1.402 → 0.837 s | 1.7× |
| BP5 CWV 平均，512²、69 層、4 時次 | 8 | 1.732 → 0.282 s | 6.1× |
| BP5 ZINT，1024²、69 層 | 8 | 1.728 → 0.276 s | 6.3× |
| BP5 降水面積平均，1024² | 8 | 0.395 → 0.0164 s | 24.1× |
| HDF5 CWV 時間平均 | 8 | 1.480 → 1.147 s | 1.3× |

### B. Derecho：6cc0735 → 92c934a，後續平均／讀取改善

| 工作 | CPU | 基準 → 優化後 | 加速 |
| --- | ---: | ---: | ---: |
| BP5 垂直平均，512²、69 層 | 1 | 3.273 → 0.237 s | 13.8× |
| BP5 面積平均 Z profile，1024²、69 層 | 1 | 13.816 → 0.672 s | 20.6× |
| HDF5 X 平均 profile，1024×69 | 1 | 0.170 → 0.0868 s | 2.0× |
| Binary 面積平均時間序列，256²、32 時次 | 4 | 0.0160 → 0.00554 s | 2.9× |

Resident-array BP5 area profile 另有 0.0613 → 0.00819 s（1 → 8 CPUs，7.5×）的平行 scaling；不含預載。HDF5 area-profile 的最後對測幾乎不變，不宣稱所有工作都有相同收益。

### C. Gaia Xeon：各工作內相對單 CPU 的 scaling

雙路 Xeon Platinum 8481C，104 實體核心／208 邏輯 CPU；GCC 13.4、HDF5 2.2.0。以下重用 pool 的時間排除首次啟動、輸出驗證與 rendering；128 workers 包含 SMT。

| 工作 | 1 CPU → 加速配置 | CPU | 加速 |
| --- | ---: | ---: | ---: |
| QVSAT 點位 Z/T profile | 8.819 → 0.126 s | 128 | 69.9× |
| ESAT 點位 Z/T profile | 8.316 → 0.137 s | 128 | 60.5× |
| 區域 AMEAN(QVSAT) profile | 33.835 → 0.632 s | 128 | 53.6× |
| 區域 AMEAN(ESAT) profile | 20.661 → 0.380 s | 128 | 54.4× |
| 完整地圖 SUM(u*v)，256 時間 | 3.591 → 0.361 s | 32 | 9.9× |
| 完整地圖 SUM(QVSAT)，256 時間 | 4.598 → 0.398 s | 32 | 11.5× |
| 單時間全球 ZINT(u) | 446.03 → 95.17 ms | 64 | 4.69× |
| 單時間全球 CWV ZINT | 438.13 → 89.14 ms | 64 | 4.92× |

首次建立 128-worker pool 的飽和診斷約 1.26–1.53 s；不能把 0.126 s 視為新程序首次回應。大地圖受 socket 回傳與主程序有序歸約限制，64/128 workers 可能更慢。密度剖面批次讀取雖減少 HDF5 呼叫，所測端到端延遲幾乎不變。

NetCDF／靜態 DEFINE 為後續擴充，不借用這批 HDF5 scaling 數字作效能保證。現有 TIME_WORKERS 文件另記錄實資料 NetCDF 54,843 個值與 HDF5 每個平行配置 2,976 個值的零差異結果；這些是既有紀錄，不是本次重跑的大資料 benchmark。

## 7. 正確性、取消與隔離驗證

### 歷史科學運算證據

- 第一階段原始 JSON：30,801,238 個 synthetic 值，最大相對差 `1.55e-15`；8,994,976 個實資料位置，最大絕對差 `9.50e-12`、相對差 `4.26e-13`，missing masks 一致。
- 後續平均階段：56,284,434 個 synthetic 值，最大絕對差 `1.49e-8`、相對差 `3.24e-15`；另有 524,332 個獨立 binary 參考比較。修改的 reader/reducer 通過相應 ASan/UBSan。
- Gaia Stage 2／7／8／9／11 依序比對 365,933,568／43,665,440／974,324,736／1,441,792／7,407,008 個位置，值與遮罩完全一致、最大絕對差零。各組可能重疊，不將它們加總為獨立樣本數。
- 既有 16 套科學回歸與 sanitizer 紀錄包含 packed/template/reversed 資料、分數邊界、缺值、非有限值、checksum、取消、worker 故障及復原；一般跨格式歸約可能有上述浮點末位差異，不能概括成全部 bitwise identical。

### 本次 1.0.14 建置／封裝驗證

本次驗證使用 RHEL 8.7 x86_64、GCC 8.5、Python 3.8、HDF5 1.10.5。另建立沒有 `.git`、舊 object 或既有 build 目錄的全新來源副本，使用 `env -i`、新的 HOME 與 `bash --noprofile --norc`，執行 build → development regressions → package。這個本機重編譯明確重用既有 checksum-pinned 私有依賴前綴；它不是從零重建所有第三方依賴的容器測試。

- 開發回歸入口涵蓋 28 個項目，其中 NetCDF wrapper 另執行 classic/NC4 各五套時間 worker／DEFINE 測試。
- HDF5 1.10.5 不符合 optional raw-chunk decoder 的 HDF5 2.0+ 條件；相應測試應標為 SKIP，native ZINT 仍需通過。
- 封裝測試讀取五種 lossless Blosc codecs、float/double、CTL/bpopen、數個時間及其平均，並驗證 explicit-loader 下的數值、PNG、取消與故障復原。
- 原生 tmux 2.7 與私有 3.5a 的獨立 server 均驗證：另一視窗 active 時，圖片 pane 仍建立在 GrADS 視窗。完整 Term 回歸包含 plain、multipart/steps、control-mode、kitty、Konsole、sixel、iTerm2。
- 使用者已在目前 SSH/tmux+iTerm2 session 驗證封裝後的 Term，指定 `GA_TERM_PROTOCOL=iterm2` 可正常顯示。

GitHub 的實際 run、job 結果與 artifact availability 以此 PR 的 Checks 及更新後的驗證紀錄為準；workflow 設定本身不代表 CI 已通過。Ubuntu/RHEL 的兩個 job 皆需完成 packaging 和 unpacked smoke test 才算通過。macOS arm64 與 Linux aarch64 仍由既有 release matrix 驗證，本機結果不代替它們。

## 8. 重現方式與已知限制

```bash
./dev/build.sh
./dev/test.sh
./dev/package.sh
cd release-dist
sha256sum -c opengrads-hpc-1.0.14-linux-*.tar.gz.sha256
```

開發 workflow 對 push、pull_request 與 workflow_dispatch 執行完整 Linux 建置／測試／打包。既有 release workflow 維持 Linux x86_64、Linux aarch64、macOS arm64 的平台矩陣。

`GA_TIME_TRACE=1` 可確認實際 workers/PIDs；`GA_TIME_WORKERS=off`、`GA_H5_DECODE=off` 可用於對照。Worker memory caps 不涵蓋完整 DEFINE 輸出。含 file/dimension qualifiers、plug-ins、不支援函數或維度、time/ensemble-varying／超限定義的命令保留既有路徑；X/Y 動畫維持原流程。加速範圍是單節點 CPU，沒有 MPI/GPU 或命令中途自動 hybrid 重排程。

合併此 PR 後，維護者在 **Lance798/opengrads-hpc 的合併 commit** 建立 `v1.0.14`，才會啟動既有 tag release 流程。本次更新不自動合併、建立 tag 或發布 GitHub Release。

## 證據來源與文件

來源報告是本機 `tmp/updated/` 的四份資料，未將暫存目錄或大型實資料加入 Git：

| 檔案 | SHA-256 |
| --- | --- |
| `reduction-audit_summary.json` | `1dde94c1fde799871855e0091c73ba147a55e6385d50bd5b14370c05bb99d565` |
| `average-stages_summary.json` | `531fb029ef654bcce39f3cb8f0a452cc3196e32947871f5d2405f50644aefcc7` |
| `derecho-new.txt` | `09731173cc6b5337b9eb29acae89004b94a33d825cdd25be4200680fbab45bf8` |
| `SHAO_GAIA_NTU_SUMMARY.zh-TW.md` | `e7e891b8e6f413b85ede4bdd78f35daff58d0444a54a15141a70db06fd68a0d0` |

可版控文件：

- [1.0.14 release notes](https://github.com/tsengshao/opengrads-hpc/blob/feat/Accelerate/docs/RELEASE_NOTES.md)
- [完整效能資料與各階段限制](https://github.com/tsengshao/opengrads-hpc/blob/feat/Accelerate/docs/PERFORMANCE.md)
- [NetCDF/HDF5／靜態 DEFINE worker 支援規則](https://github.com/tsengshao/opengrads-hpc/blob/feat/Accelerate/docs/TIME_WORKERS.md)
- [開發環境與建置方式](https://github.com/tsengshao/opengrads-hpc/blob/feat/Accelerate/docs/INSTALL.md)
- [版本、平台與 release 發布流程](https://github.com/tsengshao/opengrads-hpc/blob/feat/Accelerate/docs/RELEASES.md)
