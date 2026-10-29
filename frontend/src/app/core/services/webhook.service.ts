import { HttpClient, HttpContext } from '@angular/common/http';
import { Injectable, inject } from '@angular/core';
import { Observable } from 'rxjs';
import { CreateWebhookRequest, WebhookDelivery, WebhookSubscription, WebhookSubscriptionCreated } from '../models';
import { ADMIN_GATED } from '../interceptors/admin-key.interceptor';
import { ApiConfigService } from './api-config.service';

/** Every route here is admin-key gated (backend/.../WebhookEndpoints.cs). */
@Injectable({ providedIn: 'root' })
export class WebhookService {
  private readonly http = inject(HttpClient);
  private readonly config = inject(ApiConfigService);
  private get base(): string {
    return this.config.baseUrl();
  }
  private get adminContext(): HttpContext {
    return new HttpContext().set(ADMIN_GATED, true);
  }

  list(): Observable<WebhookSubscription[]> {
    return this.http.get<WebhookSubscription[]>(`${this.base}/webhooks`, { context: this.adminContext });
  }

  create(request: CreateWebhookRequest): Observable<WebhookSubscriptionCreated> {
    return this.http.post<WebhookSubscriptionCreated>(`${this.base}/webhooks`, request, {
      context: this.adminContext,
    });
  }

  delete(id: number): Observable<{ ok: boolean }> {
    return this.http.delete<{ ok: boolean }>(`${this.base}/webhooks/${id}`, { context: this.adminContext });
  }

  deliveries(id: number, limit = 20): Observable<WebhookDelivery[]> {
    return this.http.get<WebhookDelivery[]>(`${this.base}/webhooks/${id}/deliveries`, {
      params: { limit },
      context: this.adminContext,
    });
  }
}
