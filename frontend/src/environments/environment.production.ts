export const environment = {
  production: true,
  // A production build is typically served from a different origin than the
  // backend. Overridden at runtime via Settings (localStorage) if a viewer
  // needs to point this build at a different backend without a rebuild.
  apiBaseUrl: '/api/v1',
};
