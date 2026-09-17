import { Component, inject, signal } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { PageHeaderComponent } from '../../shared/components/page-header/page-header.component';
import { IconComponent } from '../../shared/components/icon/icon.component';
import { AdminAuthService } from '../../core/services/admin-auth.service';
import { ApiConfigService } from '../../core/services/api-config.service';
import { HealthService } from '../../core/services/health.service';
import { NotificationService } from '../../core/services/notification.service';

@Component({
  selector: 'app-settings',
  standalone: true,
  imports: [FormsModule, PageHeaderComponent, IconComponent],
  templateUrl: './settings.component.html',
})
export class SettingsComponent {
  readonly admin = inject(AdminAuthService);
  readonly apiConfig = inject(ApiConfigService);
  private readonly health = inject(HealthService);
  private readonly notifications = inject(NotificationService);

  baseUrlDraft = this.apiConfig.baseUrl();
  adminKeyDraft = this.admin.currentKey();

  readonly testing = signal(false);
  readonly testResult = signal<'idle' | 'ok' | 'fail'>('idle');

  saveBaseUrl(): void {
    this.apiConfig.setBaseUrl(this.baseUrlDraft);
    this.notifications.success('API base URL updated.');
    this.testResult.set('idle');
  }

  resetBaseUrl(): void {
    this.apiConfig.resetToDefault();
    this.baseUrlDraft = this.apiConfig.baseUrl();
    this.notifications.info('API base URL reset to default.');
  }

  saveAdminKey(): void {
    this.admin.setKey(this.adminKeyDraft);
    this.notifications.success(this.adminKeyDraft ? 'Admin key saved for this session.' : 'Admin key cleared.');
  }

  clearAdminKey(): void {
    this.adminKeyDraft = '';
    this.admin.clear();
    this.notifications.info('Admin key cleared.');
  }

  testConnection(): void {
    this.testing.set(true);
    this.testResult.set('idle');
    this.health.get().subscribe({
      next: () => {
        this.testing.set(false);
        this.testResult.set('ok');
      },
      error: () => {
        this.testing.set(false);
        this.testResult.set('fail');
      },
    });
  }
}
