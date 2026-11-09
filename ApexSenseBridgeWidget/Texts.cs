using System;
using System.Collections.Generic;
using Windows.System.UserProfile;

namespace ApexSenseBridgeWidget
{
    internal static class Texts
    {
        private static readonly Dictionary<string, Dictionary<string, string>> All = new Dictionary<string, Dictionary<string, string>>
        {
            { "en", new Dictionary<string, string>
            {
                { "Triggers", "Trigger strength" },
                { "Grips", "Vibration strength" },
                { "Starting", "Preparing bridge" },
                { "Active", "Bridge ready" },
                { "Reconnecting", "Waiting for controller" },
                { "Stopping", "Stopping" },
                { "Stopped", "Bridge stopped" },
                { "Error", "Activation refused / error" },
                { "ProfileNone", "Standard" },
                { "Profile", "Active Remapping Profile" },
                { "Remapping", "Touchpad remapping" },
                { "Applied", "Applied" },
                { "EffectsOff", "Turn trigger effects off" },
                { "MuteGrips", "Mute grip vibration" },
                { "NoSession", "No bridge session is running." },
                { "NotApplied", "The bridge did not confirm the change" },
                { "Off", "Off" },
                { "On", "On" },
                { "Percent", "{0} %" },
                { "Remember", "Remember for this game" },
                { "RememberHint", "Applies to this game only" },
                { "Saved", "Applied and saved for this game" },
                { "TrayMissing", "The ApexSenseBridge Tray is not running. Start it to control the bridge here." },
            } },
            { "es", new Dictionary<string, string>
            {
                { "Triggers", "Fuerza de los gatillos" },
                { "Grips", "Fuerza de las vibraciones" },
                { "Starting", "Preparando el bridge" },
                { "Active", "Bridge listo" },
                { "Reconnecting", "Esperando el mando" },
                { "Stopping", "Deteniendo" },
                { "Stopped", "Bridge detenido" },
                { "Error", "Activación rechazada / error" },
                { "ProfileNone", "Estándar" },
                { "Profile", "Perfil de remapeo activo" },
                { "Remapping", "Remapeo de panel táctil" },
                { "Applied", "Aplicado" },
                { "EffectsOff", "Desactivar los efectos de los gatillos" },
                { "MuteGrips", "Silenciar la vibración de las empuñaduras" },
                { "NoSession", "No hay ninguna sesión del bridge en curso." },
                { "NotApplied", "El bridge no confirmó el cambio" },
                { "Off", "Desactivado" },
                { "On", "Activado" },
                { "Percent", "{0} %" },
                { "Remember", "Recordar para este juego" },
                { "RememberHint", "Solo se aplica a este juego" },
                { "Saved", "Aplicado y guardado para este juego" },
                { "TrayMissing", "El Tray de ApexSenseBridge no está en ejecución. Inícialo para controlar el bridge desde aquí." },
            } },
            { "fr", new Dictionary<string, string>
            {
                { "Triggers", "Force des gâchettes" },
                { "Grips", "Force des vibrations" },
                { "Starting", "Préparation du bridge" },
                { "Active", "Bridge prêt" },
                { "Reconnecting", "En attente de la manette" },
                { "Stopping", "Arrêt en cours" },
                { "Stopped", "Bridge arrêté" },
                { "Error", "Activation refusée / erreur" },
                { "ProfileNone", "Standard" },
                { "Profile", "Profil de remapping actif" },
                { "Remapping", "Remapping pavé tactile" },
                { "Applied", "Appliqué" },
                { "EffectsOff", "Couper les effets de gâchette" },
                { "MuteGrips", "Couper les vibrations des poignées" },
                { "NoSession", "Aucune session du bridge n'est en cours." },
                { "NotApplied", "Le bridge n'a pas confirmé le changement" },
                { "Off", "Désactivé" },
                { "On", "Activé" },
                { "Percent", "{0} %" },
                { "Remember", "Retenir pour ce jeu" },
                { "RememberHint", "S'applique à ce jeu uniquement" },
                { "Saved", "Appliqué et enregistré pour ce jeu" },
                { "TrayMissing", "Le Tray ApexSenseBridge n'est pas lancé. Démarrez-le pour piloter le bridge ici." },
            } },
            { "ja", new Dictionary<string, string>
            {
                { "Triggers", "トリガーの強度" },
                { "Grips", "振動の強度" },
                { "Starting", "ブリッジの準備中" },
                { "Active", "ブリッジ準備完了" },
                { "Reconnecting", "コントローラー待機中" },
                { "Stopping", "停止中" },
                { "Stopped", "ブリッジ停止" },
                { "Error", "アクティブ化拒否 / エラー" },
                { "ProfileNone", "標準" },
                { "Profile", "アクティブなリマッププロファイル" },
                { "Remapping", "タッチパッドリマップ" },
                { "Applied", "適用しました" },
                { "EffectsOff", "トリガーエフェクトをオフにする" },
                { "MuteGrips", "グリップの振動をミュート" },
                { "NoSession", "ブリッジのセッションは実行されていません。" },
                { "NotApplied", "ブリッジが変更を確認しませんでした" },
                { "Off", "オフ" },
                { "On", "オン" },
                { "Percent", "{0}%" },
                { "Remember", "このゲームに記憶する" },
                { "RememberHint", "このゲームにのみ適用されます" },
                { "Saved", "適用し、このゲーム用に保存しました" },
                { "TrayMissing", "ApexSenseBridge Tray が起動していません。ここからブリッジを操作するには起動してください。" },
            } },
            { "ko", new Dictionary<string, string>
            {
                { "Triggers", "트리거 강도" },
                { "Grips", "진동 강도" },
                { "Starting", "브리지 준비 중" },
                { "Active", "브리지 준비 완료" },
                { "Reconnecting", "컨트롤러 대기 중" },
                { "Stopping", "중지 중" },
                { "Stopped", "브리지 중지됨" },
                { "Error", "활성화 거부됨 / 오류" },
                { "ProfileNone", "표준" },
                { "Profile", "활성 리매핑 프로필" },
                { "Remapping", "터치패드 리매핑" },
                { "Applied", "적용됨" },
                { "EffectsOff", "트리거 효과 끄기" },
                { "MuteGrips", "그립 진동 끄기" },
                { "NoSession", "실행 중인 브리지 세션이 없습니다." },
                { "NotApplied", "브리지가 변경을 확인하지 않았습니다" },
                { "Off", "끔" },
                { "On", "켬" },
                { "Percent", "{0}%" },
                { "Remember", "이 게임에 기억" },
                { "RememberHint", "이 게임에만 적용됩니다" },
                { "Saved", "적용되어 이 게임용으로 저장됨" },
                { "TrayMissing", "ApexSenseBridge Tray가 실행 중이 아닙니다. 여기서 브리지를 제어하려면 먼저 실행하세요." },
            } },
            { "pt", new Dictionary<string, string>
            {
                { "Triggers", "Força dos gatilhos" },
                { "Grips", "Intensidade da vibração" },
                { "Starting", "Preparando ponte" },
                { "Active", "Ponte pronta" },
                { "Reconnecting", "Aguardando o controle" },
                { "Stopping", "Parando" },
                { "Stopped", "Ponte parada" },
                { "Error", "Ativação recusada / erro" },
                { "ProfileNone", "Padrão" },
                { "Profile", "Perfil de Remapeamento Ativo" },
                { "Remapping", "Remapeamento do touchpad" },
                { "Applied", "Aplicado" },
                { "EffectsOff", "Desativar os efeitos dos gatilhos" },
                { "MuteGrips", "Silenciar a vibração das empunhaduras" },
                { "NoSession", "Nenhuma sessão do bridge está em execução." },
                { "NotApplied", "O bridge não confirmou a alteração" },
                { "Off", "Desativado" },
                { "On", "Ativado" },
                { "Percent", "{0}%" },
                { "Remember", "Lembrar para este jogo" },
                { "RememberHint", "Aplica-se apenas a este jogo" },
                { "Saved", "Aplicado e salvo para este jogo" },
                { "TrayMissing", "O Tray do ApexSenseBridge não está em execução. Inicie-o para controlar o bridge aqui." },
            } },
            { "ru", new Dictionary<string, string>
            {
                { "Triggers", "Сила триггеров" },
                { "Grips", "Сила вибрации" },
                { "Starting", "Подготовка моста" },
                { "Active", "Мост готов" },
                { "Reconnecting", "Ожидание геймпада" },
                { "Stopping", "Остановка" },
                { "Stopped", "Мост остановлен" },
                { "Error", "Отказ активации / ошибка" },
                { "ProfileNone", "Стандартный" },
                { "Profile", "Активный профиль переназначения" },
                { "Remapping", "Переназначение тачпада" },
                { "Applied", "Применено" },
                { "EffectsOff", "Отключить эффекты курков" },
                { "MuteGrips", "Отключить вибрацию рукояток" },
                { "NoSession", "Сеанс моста не запущен." },
                { "NotApplied", "Мост не подтвердил изменение" },
                { "Off", "Выкл." },
                { "On", "Вкл." },
                { "Percent", "{0} %" },
                { "Remember", "Запомнить для этой игры" },
                { "RememberHint", "Действует только для этой игры" },
                { "Saved", "Применено и сохранено для этой игры" },
                { "TrayMissing", "ApexSenseBridge Tray не запущен. Запустите его, чтобы управлять мостом отсюда." },
            } },
            { "vi", new Dictionary<string, string>
            {
                { "Triggers", "Lực phản hồi của cò" },
                { "Grips", "Cường độ rung" },
                { "Starting", "Đang chuẩn bị cầu nối" },
                { "Active", "Cầu nối sẵn sàng" },
                { "Reconnecting", "Đang chờ tay cầm" },
                { "Stopping", "Đang dừng" },
                { "Stopped", "Cầu nối đã dừng" },
                { "Error", "Từ chối kích hoạt / lỗi" },
                { "ProfileNone", "Tiêu chuẩn" },
                { "Profile", "Cấu hình gán phím đang hoạt động" },
                { "Remapping", "Gán lại bàn di chuột" },
                { "Applied", "Đã áp dụng" },
                { "EffectsOff", "Tắt hiệu ứng cò" },
                { "MuteGrips", "Tắt rung ở tay cầm" },
                { "NoSession", "Không có phiên bridge nào đang chạy." },
                { "NotApplied", "Bridge chưa xác nhận thay đổi" },
                { "Off", "Tắt" },
                { "On", "Bật" },
                { "Percent", "{0}%" },
                { "Remember", "Ghi nhớ cho trò chơi này" },
                { "RememberHint", "Chỉ áp dụng cho trò chơi này" },
                { "Saved", "Đã áp dụng và lưu cho trò chơi này" },
                { "TrayMissing", "ApexSenseBridge Tray chưa chạy. Hãy khởi động để điều khiển bridge tại đây." },
            } },
            { "zh", new Dictionary<string, string>
            {
                { "Triggers", "扳机力度" },
                { "Grips", "振动力度" },
                { "Starting", "正在准备桥接" },
                { "Active", "桥接已就绪" },
                { "Reconnecting", "正在等待手柄" },
                { "Stopping", "正在停止" },
                { "Stopped", "桥接已停止" },
                { "Error", "激活被拒绝 / 错误" },
                { "ProfileNone", "标准" },
                { "Profile", "当前映射配置" },
                { "Remapping", "触控板映射" },
                { "Applied", "已应用" },
                { "EffectsOff", "关闭扳机效果" },
                { "MuteGrips", "静音握把振动" },
                { "NoSession", "当前没有运行中的桥接会话。" },
                { "NotApplied", "桥接未确认此更改" },
                { "Off", "关" },
                { "On", "开" },
                { "Percent", "{0}%" },
                { "Remember", "为此游戏记住" },
                { "RememberHint", "仅适用于此游戏" },
                { "Saved", "已应用，并已为此游戏保存" },
                { "TrayMissing", "ApexSenseBridge Tray 未运行。请先启动它，以便在此控制桥接。" },
            } },
        };

        private static string language = Detect();

        private static string Detect()
        {
            try
            {
                var languages = GlobalizationPreferences.Languages;
                if (languages.Count > 0) return Normalize(languages[0]);
            }
            catch
            {
            }
            return "en";
        }

        private static string Normalize(string code)
        {
            if (string.IsNullOrEmpty(code)) return "en";
            var primary = code.Split('-')[0].ToLowerInvariant();
            return All.ContainsKey(primary) ? primary : "en";
        }

        public static bool SetLanguage(string code)
        {
            if (string.IsNullOrEmpty(code)) return false;
            var next = Normalize(code);
            if (next == language) return false;
            language = next;
            return true;
        }

        public static string Get(string key)
        {
            string value;
            if (All[language].TryGetValue(key, out value)) return value;
            return All["en"].TryGetValue(key, out value) ? value : key;
        }
    }
}
